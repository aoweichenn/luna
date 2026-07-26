#ifndef LUNA_TEST_H
#define LUNA_TEST_H

#include <stdbool.h>

bool luna_test_expect(bool condition, const char *expression, const char *file,
                      int line);

#define LUNA_TEST_EXPECT(expression)                                           \
    luna_test_expect((expression), #expression, __FILE__, __LINE__)

bool luna_test_arena(void);
bool luna_test_lexer(void);

#endif
