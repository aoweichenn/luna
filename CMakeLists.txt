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

if(BUILD_TESTING)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    find_package(GTest 1.14 CONFIG REQUIRED)
    include(GoogleTest)

    add_library(luna_test_support STATIC
        tests/unit/test_support.cpp
    )
    target_link_libraries(luna_test_support PUBLIC luna_compiler)
    target_include_directories(luna_test_support PUBLIC tests/unit)

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
    endif()

    add_executable(luna_unit_tests
        tests/unit/arena_test.cpp
        tests/unit/buffer_test.cpp
        tests/unit/diagnostic_test.cpp
        tests/unit/ir_test.cpp
        tests/unit/lexer_test.cpp
        tests/unit/parser_test.cpp
        tests/unit/sema_test.cpp
        tests/unit/source_test.cpp
        tests/unit/x86_64_test.cpp
    )
    target_link_libraries(luna_unit_tests
        PRIVATE
            luna_test_support
            GTest::gtest_main
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
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_integration.py"
            --compiler "$<TARGET_FILE:lunac>"
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
        NAME random.differential
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_random.py"
            --compiler "$<TARGET_FILE:lunac>"
            --work-dir "${CMAKE_CURRENT_BINARY_DIR}/random"
            --seed 0x4c554e41
            --cases 64
    )
    set_tests_properties(
        random.differential
        PROPERTIES
            LABELS "random"
            SKIP_RETURN_CODE 77
            TIMEOUT 120
    )
endif()

if(LUNA_BUILD_FUZZERS)
    add_executable(luna_frontend_fuzz tests/fuzz/frontend_fuzz.cpp)
    target_link_libraries(luna_frontend_fuzz PRIVATE luna_compiler)
    target_include_directories(luna_frontend_fuzz PRIVATE tests/unit)
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
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/nested_call.luna"
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
