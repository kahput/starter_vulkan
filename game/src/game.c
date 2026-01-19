#include "game_interface.h"

#include "renderer.h"
#include "renderer/backend/vulkan_api.h"

static GameInterface interface;

static uint32_t shader_count = 0;

typedef struct {
	uint32_t pbr_shader;

} GameState;

static GameState *state;

bool game_on_load(GameContext *context) {
	return true;
}
bool game_on_update(GameContext *context, float dt) {
	vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
	vulkan_renderer_resource_group_write(
		context->vk_context, 0, 0, 0, sizeof(vec4), &color, true);

	return true;
}
bool game_on_unload(GameContext *context) { return true; }

GameInterface *game_hookup(VulkanContext *context, void *memory, size_t size) {
	interface = (GameInterface){
		.on_load = game_on_load,
		.on_update = game_on_update,
		.on_unload = game_on_unload,
	};

	return &interface;
}
