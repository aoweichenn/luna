cmake_minimum_required(VERSION 3.25)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" LUNA_PROJECT_VERSION)
string(STRIP "${LUNA_PROJECT_VERSION}" LUNA_PROJECT_VERSION)
if(NOT LUNA_PROJECT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semantic release version")
endif()

project(
    luna
    VERSION "${LUNA_PROJECT_VERSION}"
    DESCRIPTION "A small C23-bootstrapped systems language"
    LANGUAGES C
)

option(LUNA_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LUNA_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(LUNA_ENABLE_COVERAGE "Enable source coverage instrumentation" OFF)
option(LUNA_BUILD_FUZZERS "Build coverage-guided fuzz targets" OFF)

include(CTest)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(BUILD_TESTING OR LUNA_BUILD_FUZZERS)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

add_library(luna_compiler STATIC
    src/target/target.c
    src/backend/debug/debug_ir.c
    src/frontend/support/arena.c
    src/frontend/ast/ast.c
    src/frontend/support/buffer.c
    src/frontend/compiler/compiler.c
    src/frontend/diagnostic/diagnostic.c
    src/middleend/ir/ir.c
    src/middleend/module/metadata.c
    src/middleend/module/module.c
    src/frontend/lexer/lexer.c
    src/frontend/parser/parser.c
    src/middleend/sema/sema_emit.c
    src/middleend/sema/sema_expr.c
    src/middleend/sema/sema_module.c
    src/middleend/sema/sema_scope.c
    src/middleend/sema/sema_stmt.c
    src/middleend/sema/sema_types.c
    src/frontend/source/source.c
    src/frontend/support/string_view.c
    src/frontend/token/token.c
    src/frontend/type/type.c
    src/backend/x86_64/abi.c
    src/backend/x86_64/abi_print.c
    src/backend/x86_64/abi_verify.c
    src/backend/x86_64/assembler.c
    src/backend/x86_64/backend.c
    src/backend/x86_64/elf_object.c
    src/backend/x86_64/instruction_encoder.c
    src/backend/x86_64/linker/elf_executable.c
    src/backend/x86_64/linker/dwarf.c
    src/backend/x86_64/linker/elf_layout.c
    src/backend/x86_64/linker/elf_linker.c
    src/backend/x86_64/linker/elf_reader.c
    src/backend/x86_64/linker/elf_relocation.c
    src/backend/x86_64/linker/elf_symbols.c
    src/backend/x86_64/instruction_rewrite.c
    src/backend/x86_64/instruction_rewrite_print.c
    src/backend/x86_64/instruction_rewrite_verify.c
    src/backend/x86_64/liveness.c
    src/backend/x86_64/liveness_print.c
    src/backend/x86_64/liveness_verify.c
    src/backend/x86_64/machine_ir.c
    src/backend/x86_64/machine_ir_print.c
    src/backend/x86_64/machine_ir_verify.c
    src/backend/x86_64/register_allocation.c
    src/backend/x86_64/register_allocation_print.c
    src/backend/x86_64/register_allocation_verify.c
    src/backend/x86_64/x86_64.c
    src/runtime/x86_64/linux_syscall.c
)

target_include_directories(luna_compiler PUBLIC include)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(luna_compiler PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wformat=2
        -Wundef
        -Wcast-qual
        -Wstrict-prototypes
        -Wmissing-prototypes
    )
endif()

if(LUNA_ENABLE_ASAN AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(luna_compiler PUBLIC
        -fno-omit-frame-pointer
        -fsanitize=address
    )
    target_link_options(luna_compiler PUBLIC
        -fno-omit-frame-pointer
        -fsanitize=address
    )
endif()

if(LUNA_ENABLE_UBSAN AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(luna_compiler PUBLIC
        -fno-omit-frame-pointer
        -fsanitize=undefined
    )
    target_link_options(luna_compiler PUBLIC
        -fno-omit-frame-pointer
        -fsanitize=undefined
    )
endif()

if(LUNA_ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(luna_compiler PUBLIC --coverage)
    target_link_options(luna_compiler PUBLIC --coverage)
endif()

if(LUNA_BUILD_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang" OR
       NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "LUNA_BUILD_FUZZERS requires Clang")
    endif()

    target_compile_options(luna_compiler PRIVATE
        -fno-omit-frame-pointer
        -fsanitize=fuzzer-no-link,undefined
    )
    target_link_options(luna_compiler PUBLIC
        -fno-omit-frame-pointer
        -fsanitize=fuzzer-no-link,undefined
    )
endif()

add_executable(lunac src/frontend/compiler/main.c)
target_link_libraries(lunac PRIVATE luna_compiler)

add_executable(lunalink src/backend/x86_64/linker/main.c)
target_link_libraries(lunalink PRIVATE luna_compiler)

set(LUNA_SYSCALL_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/linux/syscall.lmi"
)
set(LUNA_RUNTIME_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/runtime.lmi"
)
set(LUNA_RUNTIME_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/runtime.o"
)
set(LUNA_STD_MEMORY_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/std/memory.lmi"
)
set(LUNA_STD_MEMORY_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/std/memory.o"
)
set(LUNA_STD_BYTES_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/std/bytes.lmi"
)
set(LUNA_STD_BYTES_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/std/bytes.o"
)
set(LUNA_STD_TEXT_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/std/text.lmi"
)
set(LUNA_STD_TEXT_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/std/text.o"
)
set(LUNA_STD_PATH_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/std/path.lmi"
)
set(LUNA_STD_PATH_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/std/path.o"
)
set(LUNA_STD_IO_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/std/io.lmi"
)
set(LUNA_STD_IO_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/std/io.o"
)
set(LUNA_BOOTSTRAP_LEXER_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/frontend/lexer.lmi"
)
set(LUNA_BOOTSTRAP_LEXER_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/frontend/lexer.o"
)
set(LUNA_BOOTSTRAP_PARSER_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/frontend/parser.lmi"
)
set(LUNA_BOOTSTRAP_PARSER_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/frontend/parser.o"
)
set(LUNA_BOOTSTRAP_TYPE_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/middleend/type.lmi"
)
set(LUNA_BOOTSTRAP_TYPE_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/middleend/type.o"
)
set(LUNA_BOOTSTRAP_IR_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/middleend/ir.lmi"
)
set(LUNA_BOOTSTRAP_IR_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/middleend/ir.o"
)
set(LUNA_BOOTSTRAP_SEMA_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/middleend/sema.lmi"
)
set(LUNA_BOOTSTRAP_SEMA_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/middleend/sema.o"
)
set(LUNA_BOOTSTRAP_X86_64_TEXT_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/text.lmi"
)
set(LUNA_BOOTSTRAP_X86_64_TEXT_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/text.o"
)
set(LUNA_BOOTSTRAP_X86_64_ABI_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/abi.lmi"
)
set(LUNA_BOOTSTRAP_X86_64_ABI_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/abi.o"
)
set(LUNA_BOOTSTRAP_X86_64_FRAME_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/frame.lmi"
)
set(LUNA_BOOTSTRAP_X86_64_FRAME_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/frame.o"
)
set(LUNA_BOOTSTRAP_X86_64_CODEGEN_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/codegen.lmi"
)
set(LUNA_BOOTSTRAP_X86_64_CODEGEN_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/codegen.o"
)
set(LUNA_BOOTSTRAP_X86_64_OBJECT_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/object.lmi"
)
set(LUNA_BOOTSTRAP_X86_64_OBJECT_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/object.o"
)
set(LUNA_BOOTSTRAP_X86_64_ASSEMBLER_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/assembler.lmi"
)
set(LUNA_BOOTSTRAP_X86_64_ASSEMBLER_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/assembler.o"
)
set(LUNA_BOOTSTRAP_X86_64_LINKER_METADATA
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/linker.lmi"
)
set(LUNA_BOOTSTRAP_X86_64_LINKER_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/bootstrap/backend/x86_64/linker.o"
)
add_custom_command(
    OUTPUT "${LUNA_SYSCALL_METADATA}"
    COMMAND
        "${CMAKE_COMMAND}" -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/sysroot/luna/linux"
    COMMAND
        "$<TARGET_FILE:lunac>"
        --compile-module luna.linux.syscall
        --emit metadata
        -o "${LUNA_SYSCALL_METADATA}"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/luna/linux/syscall.interface.luna"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/luna/linux/syscall.luna"
    DEPENDS
        lunac
        runtime/luna/linux/syscall.interface.luna
        runtime/luna/linux/syscall.luna
    COMMENT "Generating the Luna Linux syscall ABI module metadata"
    VERBATIM
)

function(
    luna_add_sysroot_module
    module_name
    interface_source
    implementation_source
    metadata_output
    object_output
)
    get_filename_component(metadata_directory "${metadata_output}" DIRECTORY)
    add_custom_command(
        OUTPUT "${metadata_output}"
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory "${metadata_directory}"
        COMMAND
            "$<TARGET_FILE:lunac>"
            --compile-module "${module_name}"
            --emit metadata
            -o "${metadata_output}"
            "${CMAKE_CURRENT_SOURCE_DIR}/${interface_source}"
            "${CMAKE_CURRENT_SOURCE_DIR}/${implementation_source}"
            ${ARGN}
        DEPENDS
            lunac
            "${interface_source}"
            "${implementation_source}"
            ${ARGN}
        COMMENT "Generating ${module_name} metadata"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )
    add_custom_command(
        OUTPUT "${object_output}"
        COMMAND
            "$<TARGET_FILE:lunac>"
            --compile-module "${module_name}"
            --emit obj
            -o "${object_output}"
            "${metadata_output}"
            "${CMAKE_CURRENT_SOURCE_DIR}/${implementation_source}"
            ${ARGN}
        DEPENDS
            lunac
            "${metadata_output}"
            "${implementation_source}"
            ${ARGN}
        COMMENT "Generating ${module_name} object"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )
endfunction()

luna_add_sysroot_module(
    luna.runtime
    runtime/luna/runtime.interface.luna
    runtime/luna/runtime.luna
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_RUNTIME_OBJECT}"
    "${LUNA_SYSCALL_METADATA}"
)
luna_add_sysroot_module(
    luna.std.memory
    runtime/luna/std/memory.interface.luna
    runtime/luna/std/memory.luna
    "${LUNA_STD_MEMORY_METADATA}"
    "${LUNA_STD_MEMORY_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
)
luna_add_sysroot_module(
    luna.std.bytes
    runtime/luna/std/bytes.interface.luna
    runtime/luna/std/bytes.luna
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_BYTES_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_MEMORY_METADATA}"
)
luna_add_sysroot_module(
    luna.std.text
    runtime/luna/std/text.interface.luna
    runtime/luna/std/text.luna
    "${LUNA_STD_TEXT_METADATA}"
    "${LUNA_STD_TEXT_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
)
luna_add_sysroot_module(
    luna.std.path
    runtime/luna/std/path.interface.luna
    runtime/luna/std/path.luna
    "${LUNA_STD_PATH_METADATA}"
    "${LUNA_STD_PATH_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
)
luna_add_sysroot_module(
    luna.std.io
    runtime/luna/std/io.interface.luna
    runtime/luna/std/io.luna
    "${LUNA_STD_IO_METADATA}"
    "${LUNA_STD_IO_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
    "${LUNA_STD_PATH_METADATA}"
)
luna_add_sysroot_module(
    luna.bootstrap.frontend.lexer
    runtime/luna/bootstrap/frontend/lexer.interface.luna
    runtime/luna/bootstrap/frontend/lexer.luna
    "${LUNA_BOOTSTRAP_LEXER_METADATA}"
    "${LUNA_BOOTSTRAP_LEXER_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
)
luna_add_sysroot_module(
    luna.bootstrap.frontend.parser
    runtime/luna/bootstrap/frontend/parser.interface.luna
    runtime/luna/bootstrap/frontend/parser.luna
    "${LUNA_BOOTSTRAP_PARSER_METADATA}"
    "${LUNA_BOOTSTRAP_PARSER_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
    "${LUNA_BOOTSTRAP_LEXER_METADATA}"
)
luna_add_sysroot_module(
    luna.bootstrap.middleend.type
    runtime/luna/bootstrap/middleend/type/type.interface.luna
    runtime/luna/bootstrap/middleend/type/type.luna
    "${LUNA_BOOTSTRAP_TYPE_METADATA}"
    "${LUNA_BOOTSTRAP_TYPE_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
    "${LUNA_BOOTSTRAP_LEXER_METADATA}"
)
luna_add_sysroot_module(
    luna.bootstrap.middleend.ir
    runtime/luna/bootstrap/middleend/ir/ir.interface.luna
    runtime/luna/bootstrap/middleend/ir/ir.luna
    "${LUNA_BOOTSTRAP_IR_METADATA}"
    "${LUNA_BOOTSTRAP_IR_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
    "${LUNA_BOOTSTRAP_LEXER_METADATA}"
    "${LUNA_BOOTSTRAP_TYPE_METADATA}"
)
luna_add_sysroot_module(
    luna.bootstrap.middleend.sema
    runtime/luna/bootstrap/middleend/sema/sema.interface.luna
    runtime/luna/bootstrap/middleend/sema/sema.luna
    "${LUNA_BOOTSTRAP_SEMA_METADATA}"
    "${LUNA_BOOTSTRAP_SEMA_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
    "${LUNA_BOOTSTRAP_LEXER_METADATA}"
    "${LUNA_BOOTSTRAP_PARSER_METADATA}"
    "${LUNA_BOOTSTRAP_TYPE_METADATA}"
    "${LUNA_BOOTSTRAP_IR_METADATA}"
)
luna_add_sysroot_module(
    luna.bootstrap.backend.x86_64.text
    runtime/luna/bootstrap/backend/x86_64/text/text.interface.luna
    runtime/luna/bootstrap/backend/x86_64/text/text.luna
    "${LUNA_BOOTSTRAP_X86_64_TEXT_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_TEXT_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
)
luna_add_sysroot_module(
    luna.bootstrap.backend.x86_64.abi
    runtime/luna/bootstrap/backend/x86_64/abi/abi.interface.luna
    runtime/luna/bootstrap/backend/x86_64/abi/abi.luna
    "${LUNA_BOOTSTRAP_X86_64_ABI_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_ABI_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
    "${LUNA_BOOTSTRAP_LEXER_METADATA}"
    "${LUNA_BOOTSTRAP_TYPE_METADATA}"
    "${LUNA_BOOTSTRAP_IR_METADATA}"
)
luna_add_sysroot_module(
    luna.bootstrap.backend.x86_64.frame
    runtime/luna/bootstrap/backend/x86_64/frame/frame.interface.luna
    runtime/luna/bootstrap/backend/x86_64/frame/frame.luna
    "${LUNA_BOOTSTRAP_X86_64_FRAME_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_FRAME_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
    "${LUNA_BOOTSTRAP_LEXER_METADATA}"
    "${LUNA_BOOTSTRAP_TYPE_METADATA}"
    "${LUNA_BOOTSTRAP_IR_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_ABI_METADATA}"
)
luna_add_sysroot_module(
    luna.bootstrap.backend.x86_64.codegen
    runtime/luna/bootstrap/backend/x86_64/codegen/codegen.interface.luna
    runtime/luna/bootstrap/backend/x86_64/codegen/codegen.luna
    "${LUNA_BOOTSTRAP_X86_64_CODEGEN_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_CODEGEN_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
    "${LUNA_BOOTSTRAP_LEXER_METADATA}"
    "${LUNA_BOOTSTRAP_PARSER_METADATA}"
    "${LUNA_BOOTSTRAP_TYPE_METADATA}"
    "${LUNA_BOOTSTRAP_IR_METADATA}"
    "${LUNA_BOOTSTRAP_SEMA_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_TEXT_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_ABI_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_FRAME_METADATA}"
)
luna_add_sysroot_module(
    luna.bootstrap.backend.x86_64.object
    runtime/luna/bootstrap/backend/x86_64/object/object.interface.luna
    runtime/luna/bootstrap/backend/x86_64/object/object.luna
    "${LUNA_BOOTSTRAP_X86_64_OBJECT_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_OBJECT_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
)
luna_add_sysroot_module(
    luna.bootstrap.backend.x86_64.assembler
    runtime/luna/bootstrap/backend/x86_64/assembler/assembler.interface.luna
    runtime/luna/bootstrap/backend/x86_64/assembler/assembler.luna
    "${LUNA_BOOTSTRAP_X86_64_ASSEMBLER_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_ASSEMBLER_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_OBJECT_METADATA}"
)
luna_add_sysroot_module(
    luna.bootstrap.backend.x86_64.linker
    runtime/luna/bootstrap/backend/x86_64/linker/linker.interface.luna
    runtime/luna/bootstrap/backend/x86_64/linker/linker.luna
    "${LUNA_BOOTSTRAP_X86_64_LINKER_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_LINKER_OBJECT}"
    "${LUNA_RUNTIME_METADATA}"
    "${LUNA_STD_BYTES_METADATA}"
    "${LUNA_STD_TEXT_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_OBJECT_METADATA}"
    "${LUNA_BOOTSTRAP_X86_64_ASSEMBLER_METADATA}"
)
add_custom_target(
    luna_sysroot ALL
    DEPENDS
        "${LUNA_SYSCALL_METADATA}"
        "${LUNA_RUNTIME_METADATA}"
        "${LUNA_RUNTIME_OBJECT}"
        "${LUNA_STD_MEMORY_METADATA}"
        "${LUNA_STD_MEMORY_OBJECT}"
        "${LUNA_STD_BYTES_METADATA}"
        "${LUNA_STD_BYTES_OBJECT}"
        "${LUNA_STD_TEXT_METADATA}"
        "${LUNA_STD_TEXT_OBJECT}"
        "${LUNA_STD_PATH_METADATA}"
        "${LUNA_STD_PATH_OBJECT}"
        "${LUNA_STD_IO_METADATA}"
        "${LUNA_STD_IO_OBJECT}"
        "${LUNA_BOOTSTRAP_LEXER_METADATA}"
        "${LUNA_BOOTSTRAP_LEXER_OBJECT}"
        "${LUNA_BOOTSTRAP_PARSER_METADATA}"
        "${LUNA_BOOTSTRAP_PARSER_OBJECT}"
        "${LUNA_BOOTSTRAP_TYPE_METADATA}"
        "${LUNA_BOOTSTRAP_TYPE_OBJECT}"
        "${LUNA_BOOTSTRAP_IR_METADATA}"
        "${LUNA_BOOTSTRAP_IR_OBJECT}"
        "${LUNA_BOOTSTRAP_SEMA_METADATA}"
        "${LUNA_BOOTSTRAP_SEMA_OBJECT}"
        "${LUNA_BOOTSTRAP_X86_64_TEXT_METADATA}"
        "${LUNA_BOOTSTRAP_X86_64_TEXT_OBJECT}"
        "${LUNA_BOOTSTRAP_X86_64_ABI_METADATA}"
        "${LUNA_BOOTSTRAP_X86_64_ABI_OBJECT}"
        "${LUNA_BOOTSTRAP_X86_64_FRAME_METADATA}"
        "${LUNA_BOOTSTRAP_X86_64_FRAME_OBJECT}"
        "${LUNA_BOOTSTRAP_X86_64_CODEGEN_METADATA}"
        "${LUNA_BOOTSTRAP_X86_64_CODEGEN_OBJECT}"
        "${LUNA_BOOTSTRAP_X86_64_OBJECT_METADATA}"
        "${LUNA_BOOTSTRAP_X86_64_OBJECT_OBJECT}"
        "${LUNA_BOOTSTRAP_X86_64_ASSEMBLER_METADATA}"
        "${LUNA_BOOTSTRAP_X86_64_ASSEMBLER_OBJECT}"
        "${LUNA_BOOTSTRAP_X86_64_LINKER_METADATA}"
        "${LUNA_BOOTSTRAP_X86_64_LINKER_OBJECT}"
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(lunac PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wformat=2
        -Wundef
        -Wcast-qual
        -Wstrict-prototypes
        -Wmissing-prototypes
    )
    target_compile_options(lunalink PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wformat=2
        -Wundef
        -Wcast-qual
        -Wstrict-prototypes
        -Wmissing-prototypes
    )
endif()

if(BUILD_TESTING)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    find_package(GTest 1.14 CONFIG REQUIRED)
    include(GoogleTest)

    add_custom_target(
        luna_selfhost_toolchain
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_bootstrap_reproducibility.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --sysroot "${CMAKE_CURRENT_BINARY_DIR}/sysroot"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/selfhost-bootstrap"
        DEPENDS
            lunac
            lunalink
            luna_sysroot
        COMMENT
            "Building and verifying the complete self-hosted Luna toolchain"
        VERBATIM
    )

    set(
        LUNA_BOOTSTRAP_SEED_NAME
        "luna-bootstrap-seed-${PROJECT_VERSION}-x86_64-unknown-linux-gnu.tar"
    )
    set(
        LUNA_BOOTSTRAP_SEED_ARCHIVE
        "${CMAKE_CURRENT_BINARY_DIR}/selfhost-bootstrap/dist/${LUNA_BOOTSTRAP_SEED_NAME}"
    )
    set(
        LUNA_BOOTSTRAP_SEED_CHECKSUM
        "${LUNA_BOOTSTRAP_SEED_ARCHIVE}.sha256"
    )
    set(
        LUNA_BOOTSTRAP_SEED_EXPECTED_CHECKSUM
        "${CMAKE_CURRENT_SOURCE_DIR}/release/seeds/${LUNA_BOOTSTRAP_SEED_NAME}.sha256"
    )
    add_custom_target(
        luna_bootstrap_seed
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/release/bootstrap_seed.py"
            verify
            "${LUNA_BOOTSTRAP_SEED_ARCHIVE}"
            --checksum-file "${LUNA_BOOTSTRAP_SEED_CHECKSUM}"
            --expected-version "${PROJECT_VERSION}"
            --expected-target "x86_64-unknown-linux-gnu"
        COMMAND
            "${CMAKE_COMMAND}" -E compare_files
            "${LUNA_BOOTSTRAP_SEED_CHECKSUM}"
            "${LUNA_BOOTSTRAP_SEED_EXPECTED_CHECKSUM}"
        DEPENDS
            luna_selfhost_toolchain
        COMMENT
            "Verifying the versioned reproducible Luna bootstrap seed"
        VERBATIM
    )

    add_library(luna_test_support STATIC
        tests/unit/support/test_support.cpp
    )
    target_link_libraries(luna_test_support PUBLIC luna_compiler)
    target_include_directories(luna_test_support PUBLIC tests/unit/support)

    add_executable(luna_bootstrap_assembler
        tests/integration/bootstrap_assembler.cpp
    )
    target_link_libraries(luna_bootstrap_assembler PRIVATE luna_compiler)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(luna_test_support PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wformat=2
            -Wundef
            -Wcast-qual
            -Wold-style-cast
            -Wnon-virtual-dtor
            -Woverloaded-virtual
        )
        target_compile_options(luna_bootstrap_assembler PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wformat=2
            -Wundef
            -Wcast-qual
            -Wold-style-cast
        )
    endif()

    add_executable(luna_unit_tests
        tests/unit/backend/debug_ir_test.cpp
        tests/unit/runtime/linux_syscall_test.cpp
        tests/unit/runtime/freestanding_runtime_test.cpp
        tests/unit/stdlib/minimum_standard_library_test.cpp
        tests/unit/bootstrap/frontend_test.cpp
        tests/unit/bootstrap/middleend_test.cpp
        tests/unit/bootstrap/x86_64_backend_test.cpp
        tests/unit/support/arena_test.cpp
        tests/unit/support/buffer_test.cpp
        tests/unit/frontend/diagnostic/diagnostic_test.cpp
        tests/unit/middleend/ir_test.cpp
        tests/unit/middleend/metadata_test.cpp
        tests/unit/frontend/lexer/lexer_test.cpp
        tests/unit/frontend/parser/parser_test.cpp
        tests/unit/middleend/sema_test.cpp
        tests/unit/middleend/module_test.cpp
        tests/unit/frontend/source/source_test.cpp
        tests/unit/frontend/type_test.cpp
        tests/unit/target/target_test.cpp
        tests/unit/backend/x86_64_abi_test.cpp
        tests/unit/backend/x86_64_liveness_test.cpp
        tests/unit/backend/x86_64_machine_ir_test.cpp
        tests/unit/backend/x86_64_register_allocation_test.cpp
        tests/unit/backend/x86_64_instruction_rewrite_test.cpp
        tests/unit/backend/x86_64_elf_linker_test.cpp
        tests/unit/backend/x86_64_elf_object_test.cpp
        tests/unit/backend/x86_64_test.cpp
    )
    target_link_libraries(luna_unit_tests
        PRIVATE
            luna_test_support
            GTest::gtest_main
    )
    target_compile_definitions(
        luna_unit_tests
        PRIVATE
            LUNA_TEST_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(luna_unit_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wformat=2
            -Wundef
            -Wcast-qual
            -Wold-style-cast
            -Wnon-virtual-dtor
            -Woverloaded-virtual
        )
    endif()

    gtest_discover_tests(
        luna_unit_tests
        TEST_PREFIX "unit."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "unit"
    )

    add_executable(luna_mutation_tests tests/fuzz/mutation_test.cpp)
    target_link_libraries(luna_mutation_tests
        PRIVATE
            luna_test_support
            GTest::gtest_main
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(luna_mutation_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wformat=2
            -Wundef
            -Wcast-qual
            -Wold-style-cast
            -Wnon-virtual-dtor
            -Woverloaded-virtual
        )
    endif()

    gtest_discover_tests(
        luna_mutation_tests
        TEST_PREFIX "random."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "random"
    )

    add_test(
        NAME integration.end_to_end
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_integration.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/integration"
    )

    set_tests_properties(
        integration.end_to_end
        PROPERTIES
            LABELS "integration"
            SKIP_RETURN_CODE 77
    )

    add_test(
        NAME integration.debug_information
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_debug_info.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/debug-information"
    )
    set_tests_properties(
        integration.debug_information
        PROPERTIES
            LABELS "integration"
            SKIP_RETURN_CODE 77
            TIMEOUT 60
    )

    add_test(
        NAME integration.linux_syscall_abi
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_linux_syscall.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --metadata "${LUNA_SYSCALL_METADATA}"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/linux-syscall-abi"
    )
    set_tests_properties(
        integration.linux_syscall_abi
        PROPERTIES
            LABELS "integration"
            RESOURCE_LOCK "luna_sysroot"
            SKIP_RETURN_CODE 77
            TIMEOUT 60
    )

    add_test(
        NAME integration.freestanding_runtime
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_freestanding_runtime.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --syscall-metadata "${LUNA_SYSCALL_METADATA}"
            --runtime-metadata "${LUNA_RUNTIME_METADATA}"
            --runtime-object "${LUNA_RUNTIME_OBJECT}"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/freestanding-runtime"
    )
    set_tests_properties(
        integration.freestanding_runtime
        PROPERTIES
            LABELS "integration"
            RESOURCE_LOCK "luna_sysroot"
            SKIP_RETURN_CODE 77
            TIMEOUT 90
    )

    add_test(
        NAME integration.minimum_standard_library
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_minimum_standard_library.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --sysroot "${CMAKE_CURRENT_BINARY_DIR}/sysroot"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/minimum-standard-library"
    )
    set_tests_properties(
        integration.minimum_standard_library
        PROPERTIES
            LABELS "integration"
            RESOURCE_LOCK "luna_sysroot"
            SKIP_RETURN_CODE 77
            TIMEOUT 120
    )

    add_test(
        NAME integration.bootstrap_frontend
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_bootstrap_frontend.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --sysroot "${CMAKE_CURRENT_BINARY_DIR}/sysroot"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-frontend"
    )

    add_test(
        NAME integration.bootstrap_middleend
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_bootstrap_middleend.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --sysroot "${CMAKE_CURRENT_BINARY_DIR}/sysroot"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-middleend"
    )
    add_test(
        NAME integration.bootstrap_x86_64_backend
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_bootstrap_x86_64_backend.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --assembler "$<TARGET_FILE:luna_bootstrap_assembler>"
            --sysroot "${CMAKE_CURRENT_BINARY_DIR}/sysroot"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-x86-64-backend"
    )
    set_tests_properties(
        integration.bootstrap_x86_64_backend
        PROPERTIES
            LABELS "integration;random;fuzz"
            RESOURCE_LOCK "luna_sysroot"
            SKIP_RETURN_CODE 77
            TIMEOUT 300
    )
    add_test(
        NAME integration.bootstrap_reproducibility
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_bootstrap_reproducibility.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --sysroot "${CMAKE_CURRENT_BINARY_DIR}/sysroot"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/bootstrap-reproducibility"
    )
    set_tests_properties(
        integration.bootstrap_reproducibility
        PROPERTIES
            LABELS "integration;bootstrap;random;reproducibility"
            RESOURCE_LOCK "luna_sysroot"
            SKIP_RETURN_CODE 77
            TIMEOUT 3600
    )
    set_tests_properties(
        integration.bootstrap_middleend
        PROPERTIES
            LABELS "integration;random;fuzz"
            RESOURCE_LOCK "luna_sysroot"
            SKIP_RETURN_CODE 77
            TIMEOUT 240
    )
    set_tests_properties(
        integration.bootstrap_frontend
        PROPERTIES
            LABELS "integration;random"
            RESOURCE_LOCK "luna_sysroot"
            SKIP_RETURN_CODE 77
            TIMEOUT 180
    )

    add_test(
        NAME unit.machine_ir_reference
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential/test_machine_ir_reference.py"
    )
    set_tests_properties(
        unit.machine_ir_reference
        PROPERTIES
            LABELS "unit"
    )

    add_test(
        NAME random.differential
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/random/run_random.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/random"
            --seed 0x4c554e41
            --cases 92
    )
    set_tests_properties(
        random.differential
        PROPERTIES
            LABELS "random"
            SKIP_RETURN_CODE 77
            TIMEOUT 120
    )

    add_test(
        NAME random.instruction_differential
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/differential/run_instruction_differential.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/instruction-differential"
            --seed 0x4c554e414952
            --cases 42
    )
    set_tests_properties(
        random.instruction_differential
        PROPERTIES
            LABELS "random"
            SKIP_RETURN_CODE 77
            TIMEOUT 180
    )

    add_test(
        NAME random.standard_library
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/random/run_standard_library_random.py"
            --compiler "$<TARGET_FILE:lunac>"
            --linker "$<TARGET_FILE:lunalink>"
            --sysroot "${CMAKE_CURRENT_BINARY_DIR}/sysroot"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/standard-library-random"
    )
    set_tests_properties(
        random.standard_library
        PROPERTIES
            LABELS "random"
            RESOURCE_LOCK "luna_sysroot"
            SKIP_RETURN_CODE 77
            TIMEOUT 90
    )
endif()

if(LUNA_BUILD_FUZZERS)
    add_executable(luna_frontend_fuzz tests/fuzz/frontend_fuzz.cpp)
    target_link_libraries(luna_frontend_fuzz PRIVATE luna_compiler)
    target_include_directories(luna_frontend_fuzz PRIVATE tests/unit/support)
    target_compile_options(luna_frontend_fuzz PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wformat=2
        -Wundef
        -Wcast-qual
        -Wold-style-cast
        -fno-omit-frame-pointer
        -fsanitize=fuzzer,undefined
    )
    target_link_options(luna_frontend_fuzz PRIVATE
        -fno-omit-frame-pointer
        -fsanitize=fuzzer,undefined
    )

    if(BUILD_TESTING)
        set(LUNA_FUZZ_CORPUS_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/frontend-fuzz-corpus"
        )
        file(MAKE_DIRECTORY "${LUNA_FUZZ_CORPUS_DIR}")
        file(
            COPY
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/minimal.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/control_flow.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/i64.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/nested_call.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/unsigned.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/narrow.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/pointer_sized.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/floating.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/floating_boundaries.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/scalar_conversions.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/structured_control_flow.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/memory.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/external.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/stack_arguments.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/aggregate.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/aggregate_by_value.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/module_pair.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/module_import.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/linux_syscall.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/freestanding_runtime.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/minimum_standard_library.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/bootstrap_frontend.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/bootstrap_middleend.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/bootstrap_x86_64_backend.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootstrap/stage_compiler.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootstrap/stage_assembler.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/bootstrap/stage_linker.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/runtime/luna/bootstrap/backend/x86_64/object/object.interface.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/runtime/luna/bootstrap/backend/x86_64/object/object.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/runtime/luna/bootstrap/backend/x86_64/assembler/assembler.interface.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/runtime/luna/bootstrap/backend/x86_64/assembler/assembler.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/runtime/luna/bootstrap/backend/x86_64/linker/linker.interface.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/runtime/luna/bootstrap/backend/x86_64/linker/linker.luna"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/object_assembly.s"
            DESTINATION "${LUNA_FUZZ_CORPUS_DIR}"
        )

        add_test(
            NAME fuzz.frontend
            COMMAND
                luna_frontend_fuzz
                -runs=5000
                -max_len=4096
                "${LUNA_FUZZ_CORPUS_DIR}"
        )
        set_tests_properties(
            fuzz.frontend
            PROPERTIES
                LABELS "fuzz"
                TIMEOUT 120
        )
    endif()
endif()
