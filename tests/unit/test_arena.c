#include "test.h"

#include "luna/arena.h"
#include "luna/string_view.h"

#include <stdint.h>
#include <string.h>

bool luna_test_arena(void) {
    LunaArena arena;
    luna_arena_init(&arena, 64U);

    uint32_t *integer =
        luna_arena_allocate_zero(&arena, sizeof(uint32_t), _Alignof(uint32_t));
    if (!LUNA_TEST_EXPECT(integer != NULL) ||
        !LUNA_TEST_EXPECT(*integer == 0U) ||
        !LUNA_TEST_EXPECT((uintptr_t)integer % _Alignof(uint32_t) == 0U)) {
        luna_arena_destroy(&arena);
        return false;
    }

    for (uint32_t index = 0U; index < 1000U; index += 1U) {
        uint64_t *value =
            luna_arena_allocate(&arena, sizeof(uint64_t), _Alignof(uint64_t));
        if (!LUNA_TEST_EXPECT(value != NULL)) {
            luna_arena_destroy(&arena);
            return false;
        }
        *value = index;
    }

    char *copy =
        luna_arena_copy_string(&arena, luna_string_view_from_c_string("luna"));
    const bool success = LUNA_TEST_EXPECT(copy != NULL) &&
                         LUNA_TEST_EXPECT(strcmp(copy, "luna") == 0);

    luna_arena_destroy(&arena);
    return success;
}
