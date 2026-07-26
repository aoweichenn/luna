cmake_minimum_required(VERSION 3.25)

project(
    luna
    VERSION 0.1.0
    DESCRIPTION "A small C23-bootstrapped systems language"
    LANGUAGES C
)

option(LUNA_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(LUNA_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(LUNA_ENABLE_COVERAGE "Enable source coverage instrumentation" OFF)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(luna_compiler STATIC
    src/arena.c
    src/ast.c
    src/buffer.c
    src/compiler.c
    src/diagnostic.c
    src/ir.c
    src/lexer.c
    src/parser.c
    src/sema.c
    src/source.c
    src/string_view.c
    src/token.c
    src/x86_64.c
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

add_executable(lunac src/main.c)
target_link_libraries(lunac PRIVATE luna_compiler)

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
endif()

include(CTest)

if(BUILD_TESTING)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

    add_executable(luna_unit_tests
        tests/unit/main.c
        tests/unit/test_arena.c
        tests/unit/test_lexer.c
    )
    target_link_libraries(luna_unit_tests PRIVATE luna_compiler)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
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
            -Wstrict-prototypes
            -Wmissing-prototypes
        )
    endif()

    add_test(NAME unit COMMAND luna_unit_tests)

    add_test(
        NAME integration
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_integration.py"
            --compiler "$<TARGET_FILE:lunac>"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/integration"
    )

    set_tests_properties(integration PROPERTIES SKIP_RETURN_CODE 77)
endif()
