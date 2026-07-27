#include "test_support.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace luna::test {
namespace {

constexpr std::size_t LUNA_TEST_METADATA_ARENA_BLOCK_SIZE =
    std::size_t{32U} * 1024U;

[[nodiscard]] bool DecodeMetadata(std::string_view bytes,
                                  LunaDiagnosticEngine *diagnostics,
                                  LunaArena *arena,
                                  LunaModuleMetadata *metadata,
                                  LunaSourceFile *source) {
    return luna_source_from_bytes("<test.lmi>", bytes.data(), bytes.size(),
                                  source) &&
           luna_module_metadata_decode(source, luna_target_info_default(),
                                       arena, diagnostics, metadata);
}

}

TEST(MetadataTest, RoundTripsTheCompleteInterfaceDeterministically) {
    FrontendHarness harness{
        "export module lib.metadata;\n"
        "import dep.api;\n"
        "export struct Packet {\n"
        "    bytes: [4]u8;\n"
        "    next: *const Packet;\n"
        "}\n"
        "export union Number {\n"
        "    integer: i64;\n"
        "    real: f64;\n"
        "}\n"
        "export enum State: i8 {\n"
        "    missing = -1,\n"
        "    idle,\n"
        "    ready = 7,\n"
        "}\n"
        "export fn inspect(packet: *const Packet) -> i32;\n"
        "fn private_value() -> i32;\n"
        "export extern fn c_identity(value: i32) -> i32;\n",
        "module lib.metadata;\n"
        "fn inspect(packet: *const Packet) -> i32 { return packet->bytes[0]; "
        "}\n"
        "fn private_value() -> i32 { return 1; }\n"};
    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();

    LunaStringBuilder encoded{};
    luna_string_builder_init(&encoded);
    const LunaModuleMetadataDependency dependencies[] = {
        {
            .module_name = luna_string_view_from_c_string("dep.api"),
            .content_hash = UINT64_C(0x123456789abcdef0),
        },
    };
    ASSERT_TRUE(luna_module_metadata_encode(
        harness.InterfaceProgram(), luna_target_info_default(), dependencies,
        1U, harness.DiagnosticEngine(), &encoded))
        << harness.Diagnostics();
    ASSERT_GT(encoded.length, 32U);

    LunaArena decode_arena{};
    luna_arena_init(&decode_arena, LUNA_TEST_METADATA_ARENA_BLOCK_SIZE);
    LunaModuleMetadata metadata{};
    LunaSourceFile metadata_source{};
    ASSERT_TRUE(DecodeMetadata(
        std::string_view{luna_string_builder_data(&encoded), encoded.length},
        harness.DiagnosticEngine(), &decode_arena, &metadata, &metadata_source))
        << harness.Diagnostics();

    const LunaProgram *interface_unit = metadata.interface_unit;
    ASSERT_NE(interface_unit, nullptr);
    EXPECT_TRUE(interface_unit->is_interface);
    EXPECT_TRUE(luna_string_view_equal_c_string(interface_unit->module_name,
                                                "lib.metadata"));
    ASSERT_NE(interface_unit->first_import, nullptr);
    EXPECT_TRUE(luna_string_view_equal_c_string(
        interface_unit->first_import->module_name, "dep.api"));
    ASSERT_EQ(metadata.dependencies.length, 1U);
    const auto *decoded_dependency =
        static_cast<const LunaModuleMetadataDependency *>(
            luna_vector_at_const(&metadata.dependencies, 0U));
    ASSERT_NE(decoded_dependency, nullptr);
    EXPECT_EQ(decoded_dependency->content_hash, UINT64_C(0x123456789abcdef0));

    const LunaTypeDeclaration *packet = interface_unit->first_type_declaration;
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->kind, LUNA_TYPE_STRUCT);
    EXPECT_TRUE(packet->is_exported);
    EXPECT_EQ(packet->as.aggregate.field_count, 2U);
    ASSERT_NE(packet->as.aggregate.first_field, nullptr);
    EXPECT_EQ(packet->as.aggregate.first_field->type.kind, LUNA_TYPE_ARRAY);
    EXPECT_EQ(packet->as.aggregate.first_field->type.as.array.count, 4U);

    const LunaTypeDeclaration *number = packet->next;
    ASSERT_NE(number, nullptr);
    EXPECT_EQ(number->kind, LUNA_TYPE_UNION);
    const LunaTypeDeclaration *state = number->next;
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->kind, LUNA_TYPE_ENUM);
    ASSERT_NE(state->as.enumeration.first_member, nullptr);
    ASSERT_NE(state->as.enumeration.first_member->initializer, nullptr);
    EXPECT_EQ(state->as.enumeration.first_member->initializer->kind,
              LUNA_EXPRESSION_UNARY);
    EXPECT_EQ(
        state->as.enumeration.first_member->initializer->as.unary.operator_kind,
        LUNA_TOKEN_MINUS);

    const LunaFunction *inspect = interface_unit->first_function;
    ASSERT_NE(inspect, nullptr);
    EXPECT_TRUE(inspect->is_exported);
    EXPECT_FALSE(inspect->is_external);
    ASSERT_NE(inspect->next, nullptr);
    EXPECT_FALSE(inspect->next->is_exported);
    ASSERT_NE(inspect->next->next, nullptr);
    EXPECT_TRUE(inspect->next->next->is_external);

    LunaStringBuilder reencoded{};
    luna_string_builder_init(&reencoded);
    ASSERT_TRUE(luna_module_metadata_encode(
        interface_unit, luna_target_info_default(),
        reinterpret_cast<const LunaModuleMetadataDependency *>(
            metadata.dependencies.data),
        static_cast<std::uint32_t>(metadata.dependencies.length),
        harness.DiagnosticEngine(), &reencoded))
        << harness.Diagnostics();
    const std::string_view reencoded_view{luna_string_builder_data(&reencoded),
                                          reencoded.length};
    const std::string_view encoded_view{luna_string_builder_data(&encoded),
                                        encoded.length};
    EXPECT_EQ(reencoded_view, encoded_view);

    luna_string_builder_destroy(&reencoded);
    luna_module_metadata_destroy(&metadata);
    luna_source_destroy(&metadata_source);
    luna_arena_destroy(&decode_arena);
    luna_string_builder_destroy(&encoded);
}

TEST(MetadataTest, LowersImportedMetadataToBodylessLunaDeclarations) {
    FrontendHarness library{
        "export module lib.precompiled;\n"
        "export struct Value { raw: i32; }\n"
        "export fn read(value: *const Value) -> i32;\n",
        "module lib.precompiled;\n"
        "fn read(value: *const Value) -> i32 { return value->raw; }\n"};
    FrontendHarness application{"module app.metadata;\n"
                                "import lib.precompiled;\n"
                                "fn main() -> i32 {\n"
                                "    let value: Value = { raw = 42, };\n"
                                "    return read(&value);\n"
                                "}\n"};
    ASSERT_TRUE(library.Parse()) << library.Diagnostics();
    ASSERT_TRUE(application.Parse()) << application.Diagnostics();

    LunaStringBuilder encoded{};
    luna_string_builder_init(&encoded);
    ASSERT_TRUE(luna_module_metadata_encode(
        library.InterfaceProgram(), luna_target_info_default(), nullptr, 0U,
        application.DiagnosticEngine(), &encoded))
        << application.Diagnostics();

    LunaArena decode_arena{};
    luna_arena_init(&decode_arena, LUNA_TEST_METADATA_ARENA_BLOCK_SIZE);
    LunaModuleMetadata metadata{};
    LunaSourceFile metadata_source{};
    ASSERT_TRUE(DecodeMetadata(
        std::string_view{luna_string_builder_data(&encoded), encoded.length},
        application.DiagnosticEngine(), &decode_arena, &metadata,
        &metadata_source))
        << application.Diagnostics();

    const LunaModuleInput inputs[] = {
        {
            .program = application.Program(),
            .is_metadata = false,
            .metadata_dependencies = nullptr,
            .metadata_dependency_count = 0U,
            .metadata_content_hash = 0U,
        },
        {
            .program = metadata.interface_unit,
            .is_metadata = true,
            .metadata_dependencies =
                reinterpret_cast<const LunaModuleMetadataDependency *>(
                    metadata.dependencies.data),
            .metadata_dependency_count =
                static_cast<std::uint32_t>(metadata.dependencies.length),
            .metadata_content_hash = metadata.content_hash,
        },
    };
    const LunaModuleOptions options = {
        .compilation_kind = LUNA_MODULE_COMPILE_EXECUTABLE,
        .root_module_name = {},
        .require_compiled_root_interface = false,
    };
    LunaIrModule module{};
    luna_ir_module_init(&module, luna_target_info_default());
    ASSERT_TRUE(luna_module_lower_inputs(
        inputs, 2U, &options, application.DiagnosticEngine(), &module))
        << application.Diagnostics();
    ASSERT_TRUE(
        luna_ir_verify(&module, application.DiagnosticEngine()->stream));
    ASSERT_EQ(module.functions.length, 2U);

    auto *imported =
        static_cast<LunaIrFunction *>(luna_vector_at(&module.functions, 0U));
    ASSERT_NE(imported, nullptr);
    EXPECT_EQ(imported->linkage, LUNA_IR_LINKAGE_MODULE_IMPORT);
    EXPECT_TRUE(imported->has_module_metadata_hash);
    EXPECT_EQ(imported->module_metadata_hash, metadata.content_hash);
    EXPECT_EQ(imported->blocks.length, 0U);

    LunaStringBuilder assembly{};
    luna_string_builder_init(&assembly);
    ASSERT_TRUE(luna_x86_64_emit_assembly(
        &module, application.DiagnosticEngine(), &assembly))
        << application.Diagnostics();
    const std::string_view assembly_text{luna_string_builder_data(&assembly),
                                         assembly.length};
    EXPECT_NE(assembly_text.find(".extern _L"), std::string_view::npos);
    EXPECT_NE(assembly_text.find("_H"), std::string_view::npos);
    EXPECT_NE(assembly_text.find("call _L"), std::string_view::npos);

    imported->has_module_metadata_hash = false;
    EXPECT_FALSE(
        luna_ir_verify(&module, application.DiagnosticEngine()->stream));
    EXPECT_NE(application.Diagnostics().find("has no module metadata identity"),
              std::string::npos);

    luna_string_builder_destroy(&assembly);
    luna_ir_module_destroy(&module);
    luna_module_metadata_destroy(&metadata);
    luna_source_destroy(&metadata_source);
    luna_arena_destroy(&decode_arena);
    luna_string_builder_destroy(&encoded);
}

TEST(MetadataTest, RejectsCorruptionAndUnsupportedVersions) {
    FrontendHarness harness{"export module lib.corruption;\n"
                            "export fn value() -> i32;\n",
                            "module lib.corruption;\n"
                            "fn value() -> i32 { return 42; }\n"};
    ASSERT_TRUE(harness.Parse()) << harness.Diagnostics();

    LunaStringBuilder encoded{};
    luna_string_builder_init(&encoded);
    ASSERT_TRUE(luna_module_metadata_encode(
        harness.InterfaceProgram(), luna_target_info_default(), nullptr, 0U,
        harness.DiagnosticEngine(), &encoded))
        << harness.Diagnostics();
    const std::string valid{luna_string_builder_data(&encoded), encoded.length};

    const auto try_decode = [&](const std::string &bytes) {
        LunaArena arena{};
        luna_arena_init(&arena, LUNA_TEST_METADATA_ARENA_BLOCK_SIZE);
        LunaModuleMetadata metadata{};
        LunaSourceFile source{};
        const bool decoded = DecodeMetadata(bytes, harness.DiagnosticEngine(),
                                            &arena, &metadata, &source);
        luna_module_metadata_destroy(&metadata);
        luna_source_destroy(&source);
        luna_arena_destroy(&arena);
        return decoded;
    };

    std::string bad_magic = valid;
    bad_magic[0] = 'X';
    EXPECT_FALSE(try_decode(bad_magic));

    std::string bad_version = valid;
    bad_version[8] = static_cast<char>(2U);
    EXPECT_FALSE(try_decode(bad_version));

    std::string bad_language_abi = valid;
    bad_language_abi[12] = static_cast<char>(2U);
    EXPECT_FALSE(try_decode(bad_language_abi));

    std::string bad_checksum = valid;
    bad_checksum.back() = static_cast<char>(
        static_cast<unsigned char>(bad_checksum.back()) ^ std::uint8_t{1U});
    EXPECT_FALSE(try_decode(bad_checksum));

    const std::string diagnostics = harness.Diagnostics();
    EXPECT_NE(diagnostics.find("bad magic"), std::string::npos);
    EXPECT_NE(diagnostics.find("unsupported module metadata format"),
              std::string::npos);
    EXPECT_NE(diagnostics.find("incompatible module metadata language ABI"),
              std::string::npos);
    EXPECT_NE(diagnostics.find("payload checksum mismatch"), std::string::npos);

    luna_string_builder_destroy(&encoded);
}

}
