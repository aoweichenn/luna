extern "C" {
#include "luna/backend/x86_64/elf_object.h"
#include "luna/frontend/diagnostic/diagnostic.h"
#include "luna/frontend/support/buffer.h"
#include "luna/frontend/support/string_view.h"
}

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {

constexpr int LUNA_BOOTSTRAP_ASSEMBLER_EXPECTED_ARGUMENT_COUNT = 3;
constexpr int LUNA_BOOTSTRAP_ASSEMBLER_USAGE_EXIT_STATUS = 64;
constexpr int LUNA_BOOTSTRAP_ASSEMBLER_INPUT_EXIT_STATUS = 65;
constexpr int LUNA_BOOTSTRAP_ASSEMBLER_FAILURE_EXIT_STATUS = 1;

[[nodiscard]] std::string ReadFile(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

[[nodiscard]] bool WriteFile(const std::filesystem::path &path,
                             const LunaStringBuilder &object) {
    if (object.length >
        static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(object.data, static_cast<std::streamsize>(object.length));
    return output.good();
}

} // 匿名命名空间

int main(int argument_count, char **arguments) {
    if (argument_count != LUNA_BOOTSTRAP_ASSEMBLER_EXPECTED_ARGUMENT_COUNT ||
        arguments == nullptr) {
        return LUNA_BOOTSTRAP_ASSEMBLER_USAGE_EXIT_STATUS;
    }
    const std::string assembly = ReadFile(arguments[1]);
    if (assembly.empty()) {
        return LUNA_BOOTSTRAP_ASSEMBLER_INPUT_EXIT_STATUS;
    }
    std::FILE *diagnostic_stream = stderr;
    LunaDiagnosticEngine diagnostics{};
    luna_diagnostic_init(&diagnostics, diagnostic_stream);
    LunaStringBuilder object{};
    luna_string_builder_init(&object);
    const bool assembled = luna_x86_64_assemble_elf_object(
        LunaStringView{
            .data = assembly.data(),
            .length = assembly.size(),
        },
        &diagnostics, &object);
    const bool verified = assembled && luna_x86_64_elf_object_verify(
                                           LunaStringView{
                                               .data = object.data,
                                               .length = object.length,
                                           },
                                           diagnostic_stream);
    const bool written = verified && WriteFile(arguments[2], object);
    luna_string_builder_destroy(&object);
    return written ? 0 : LUNA_BOOTSTRAP_ASSEMBLER_FAILURE_EXIT_STATUS;
}
