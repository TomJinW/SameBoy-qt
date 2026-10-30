#pragma once
#include <Core/gb.h>

using GameBoyAllocator = GB_gameboy_t *(*)();

// GB_alloc() in the pinned core dereferences its result before checking malloc.
// Keep that failure boundary in the Qt frontend, before calling GB_init().
inline GB_gameboy_t *allocateGameBoy(GB_model_t model, GameBoyAllocator allocator = GB_alloc)
{
    auto *gameboy = allocator();
    return gameboy ? GB_init(gameboy, model) : nullptr;
}
