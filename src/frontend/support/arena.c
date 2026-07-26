#include "luna/frontend/support/arena.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct LunaArenaBlock {
    LunaArenaBlock *next;
    size_t capacity;
    size_t used;
    max_align_t alignment;
    unsigned char data[];
};

static bool luna_is_power_of_two(size_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool luna_align_up(size_t value, size_t alignment, size_t *result) {
    const size_t mask = alignment - 1U;

    if (value > SIZE_MAX - mask) {
        return false;
    }

    *result = (value + mask) & ~mask;
    return true;
}

static LunaArenaBlock *luna_arena_new_block(size_t capacity) {
    if (capacity > SIZE_MAX - sizeof(LunaArenaBlock)) {
        return NULL;
    }

    LunaArenaBlock *block = malloc(sizeof(LunaArenaBlock) + capacity);
    if (block == NULL) {
        return NULL;
    }

    block->next = NULL;
    block->capacity = capacity;
    block->used = 0U;
    return block;
}

void luna_arena_init(LunaArena *arena, size_t default_block_size) {
    arena->first = NULL;
    arena->default_block_size =
        default_block_size == 0U ? 16U * 1024U : default_block_size;
}

void luna_arena_destroy(LunaArena *arena) {
    LunaArenaBlock *block = arena->first;

    while (block != NULL) {
        LunaArenaBlock *next = block->next;
        free(block);
        block = next;
    }

    arena->first = NULL;
}

void *luna_arena_allocate(LunaArena *arena, size_t size, size_t alignment) {
    if (!luna_is_power_of_two(alignment) || alignment > _Alignof(max_align_t)) {
        return NULL;
    }

    const size_t allocation_size = size == 0U ? 1U : size;
    LunaArenaBlock *block = arena->first;
    size_t aligned_used = 0U;

    if (block == NULL ||
        !luna_align_up(block->used, alignment, &aligned_used) ||
        aligned_used > block->capacity ||
        allocation_size > block->capacity - aligned_used) {
        size_t capacity = arena->default_block_size;
        if (capacity < allocation_size) {
            capacity = allocation_size;
        }

        if (capacity <= SIZE_MAX - alignment) {
            capacity += alignment;
        }

        LunaArenaBlock *new_block = luna_arena_new_block(capacity);
        if (new_block == NULL) {
            return NULL;
        }

        new_block->next = block;
        arena->first = new_block;
        block = new_block;

        if (!luna_align_up(block->used, alignment, &aligned_used)) {
            return NULL;
        }
    }

    void *allocation = block->data + aligned_used;
    block->used = aligned_used + allocation_size;
    return allocation;
}

void *luna_arena_allocate_zero(LunaArena *arena, size_t size,
                               size_t alignment) {
    void *allocation = luna_arena_allocate(arena, size, alignment);
    if (allocation != NULL) {
        memset(allocation, 0, size);
    }

    return allocation;
}

char *luna_arena_copy_string(LunaArena *arena, LunaStringView value) {
    if (value.length == SIZE_MAX) {
        return NULL;
    }

    char *copy = luna_arena_allocate(arena, value.length + 1U, _Alignof(char));
    if (copy == NULL) {
        return NULL;
    }

    if (value.length > 0U) {
        memcpy(copy, value.data, value.length);
    }
    copy[value.length] = '\0';
    return copy;
}
