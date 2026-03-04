#include "input.h"
#include "input/input_types.h"
#include "platform.h"
#include "scene.h"

#include <game_interface.h>

#include <common.h>
#include <core/cmath.h>
#include <core/arena.h>
#include <core/debug.h>
#include <core/logger.h>
#include <core/astring.h>

#include <renderer.h>
#include <renderer/r_internal.h>
#include <renderer/backend/vulkan_api.h>

#include <assets.h>
#include <assets/mesh_source.h>
#include <assets/asset_types.h>

#include <math.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

typedef struct {
	float mousex, mousey;
	uint32_t mouse_down;

	uint32_t hot, active;
} UIState;

typedef struct {
	Mesh *meshes;
	uint32_t mesh_count;

	Material *materials;
	uint32_t material_count;

	Matrix4f transform;
} GameModel;

typedef struct {
	Arena arena;

	bool is_initialized;
} GameState;

static GameState *state = NULL;

FrameInfo game_on_update_and_render(GameContext *context, float dt) {
	state = (GameState *)context->permanent_memory;

	ArenaTemp scratch = arena_scratch(NULL);
	if (state->is_initialized == false) {
		state->arena = (Arena){
			.memory = state + 1,
			.offset = 0,
			.capacity = context->permanent_memory_size - sizeof(GameState)
		};

		state->is_initialized = true;
	}

	return (FrameInfo){ 0 };
}

static GameInterface interface;
GameInterface *game_hookup(void) {
	interface = (GameInterface){
		.on_update = game_on_update_and_render,
	};
	return &interface;
}
