#include "test.h"

#include <stddef.h>
#include <stdio.h>

typedef bool (*LunaTestFunction)(void);

typedef struct LunaTestCase {
    const char *name;
    LunaTestFunction function;
} LunaTestCase;

bool luna_test_expect(bool condition, const char *expression, const char *file,
                      int line) {
    if (!condition) {
        (void)fprintf(stderr, "%s:%d: test assertion failed: %s\n", file, line,
                      expression);
    }
    return condition;
}

int main(void) {
    const LunaTestCase tests[] = {
        {"arena", luna_test_arena},
        {"lexer", luna_test_lexer},
    };
    const size_t test_count = sizeof(tests) / sizeof(tests[0]);
    size_t failure_count = 0U;

    for (size_t index = 0U; index < test_count; index += 1U) {
        if (!tests[index].function()) {
            (void)fprintf(stderr, "FAILED: %s\n", tests[index].name);
            failure_count += 1U;
        } else {
            (void)fprintf(stdout, "PASS: %s\n", tests[index].name);
        }
    }

    if (failure_count != 0U) {
        (void)fprintf(stderr, "%zu of %zu unit tests failed\n", failure_count,
                      test_count);
        return 1;
    }

    return 0;
}
