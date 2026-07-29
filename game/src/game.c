#include "types.h"

#include <common.h>
#include <core/arena.h>
#include <core/logger.h>

#include <os.h>

#include <gfx.h>
#include <gfx/font.h>
#include <gfx/imgui.h>

#include <utils/input.h>
#include <utils/anim.h>

typedef struct {
	OS_Surface *surface;

	GFX_Shader *shaders[SHADER_MAX];
	Mesh meshes[MESH_MAX];

	bool initialized;
} State;

State *state = 0;

bool tick(Arena *memory, InputState *input) {
	state = (State *)memory->base;
	input_set_context(input);

	if (state->initialized == false) {
		state->surface = os_surface_open(1280, 720, s("game"), 0);

		state->initialized = true;
	}

	return true;
}
