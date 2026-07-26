#ifndef LUNA_ARENA_H
#define LUNA_ARENA_H

#include "luna/string_view.h"

#include <stddef.h>

typedef struct LunaArenaBlock LunaArenaBlock;

typedef struct LunaArena {
    LunaArenaBlock *first;
    size_t default_block_size;
} LunaArena;

void luna_arena_init(LunaArena *arena, size_t default_block_size);
void luna_arena_destroy(LunaArena *arena);
void *luna_arena_allocate(LunaArena *arena, size_t size, size_t alignment);
void *luna_arena_allocate_zero(LunaArena *arena, size_t size, size_t alignment);
char *luna_arena_copy_string(LunaArena *arena, LunaStringView value);

#endif
