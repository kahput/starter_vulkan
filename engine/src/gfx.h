#pragma once

#include "core/arena.h"

#include "gfx/gfx_types.h"

typedef struct {
	bool presenting;
} GFX_Settings;

bool gfx_startup(Arena *arena, GFX_Settings settings);
void gfx_shutdown(void);
