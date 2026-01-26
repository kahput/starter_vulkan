#include "common.h"
#include "renderer.h"
#include "renderer/backend/vulkan_api.h"
#include "game_interface.h"
#include "assets.h"
#include "assets/asset_types.h"
#include "core/arena.h"
#include "core/logger.h"
#include "renderer/r_internal.h"

#include <cglm/cglm.h>

static GameInterface interface;

typedef struct {
	Arena arena;
	// Sprite shader
	uint32_t sprite_shader;
	uint32_t sprite_material;

	// Quad mesh
	uint32_t quad_vb;
	uint32_t quad_vertex_count;

	// Texture
	uint32_t sprite_texture;

	// Resource indices
	uint32_t next_buffer_index;
	uint32_t next_shader_index;
	uint32_t next_image_index;
	uint32_t next_group_index;

	bool is_initialized;
} GameState;

static GameState *state = NULL;

UUID create_plane_mesh(Arena *arena, uint32_t subdivide_width, uint32_t subdivide_depth, MeshSource **out_mesh);

bool game_on_load(GameContext *context) {
	LOG_INFO("Game loading...");
	LOG_INFO("Game loaded successfully");
	return true;
}

bool game_on_update(GameContext *context, float dt) {
	state = (GameState *)context->game_memory;

	if (state->is_initialized == false) {
		state->arena = (Arena){
			.memory = state + 1,
			.offset = 0,
			.capacity = context->game_memory_size - sizeof(GameState)
		};

		// Initialize resource indices (start after engine defaults)
		state->next_buffer_index = 2;
		state->next_shader_index = 3;
		state->next_image_index = RENDERER_DEFAULT_SURFACE_COUNT;
		state->next_group_index = 2;

		// Load sprite shader
		ShaderSource *sprite_shader_src = NULL;

		UUID sprite_shader_id = asset_library_request_shader(context->asset_library, str_lit("sprite.glsl"), &sprite_shader_src);
		if (!sprite_shader_src) {
			LOG_ERROR("Failed to load sprite shader");
			return false;
		} 

		state->sprite_shader = state->next_shader_index++;
		ShaderConfig sprite_config = {
			.vertex_code = sprite_shader_src->vertex_shader.content,
			.vertex_code_size = sprite_shader_src->vertex_shader.size,
			.fragment_code = sprite_shader_src->fragment_shader.content,
			.fragment_code_size = sprite_shader_src->fragment_shader.size,
		};

		PipelineDesc desc = DEFAULT_PIPELINE();
		desc.cull_mode = CULL_MODE_NONE;

		ShaderReflection reflection;
		LOG_INFO("Creating shader at index %d", state->sprite_shader);
		vulkan_renderer_shader_create(&state->arena, context->vk_context, state->sprite_shader,
			RENDERER_GLOBAL_RESOURCE_MAIN, &sprite_config, &reflection);
		vulkan_renderer_shader_variant_create(context->vk_context, state->sprite_shader,
			RENDERER_DEFAULT_SHADER_VARIANT_STANDARD, RENDERER_DEFAULT_PASS_MAIN, desc);
		desc.polygon_mode = POLYGON_MODE_LINE;
		vulkan_renderer_shader_variant_create(context->vk_context, state->sprite_shader,
			RENDERER_DEFAULT_SHADER_VARIANT_WIREFRAME, RENDERER_DEFAULT_PASS_MAIN, desc);

		// Create quad mesh (simple plane facing camera)
		ArenaTemp scratch = arena_scratch(NULL);
		MeshSource *plane_src = NULL;
		create_plane_mesh(scratch.arena, 0, 0, &plane_src);

		state->quad_vb = state->next_buffer_index++;
		state->quad_vertex_count = plane_src->vertex_count;
		vulkan_renderer_buffer_create(context->vk_context, state->quad_vb, BUFFER_TYPE_VERTEX,
			sizeof(Vertex) * plane_src->vertex_count, plane_src->vertices);

		arena_release_scratch(scratch);

		// Load sprite texture
		ImageSource *sprite_src = NULL;
		UUID sprite_id = asset_library_request_image(context->asset_library, str_lit("tile_0085.png"), &sprite_src);

		if (!sprite_src) {
			LOG_ERROR("Failed to load sprite texture");
			return false;
		}

		state->sprite_texture = state->next_image_index++;
		vulkan_renderer_texture_create(context->vk_context, state->sprite_texture,
			sprite_src->width, sprite_src->height,
			TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB,
			TEXTURE_USAGE_SAMPLED, sprite_src->pixels);

		// Create material
		vec4 sprite_color = { 1.0f, 1.0f, 1.0f, 1.0f };
		state->sprite_material = state->next_group_index++;
		vulkan_renderer_resource_group_create(context->vk_context, state->sprite_material,
			state->sprite_shader, 256);
		vulkan_renderer_resource_group_write(context->vk_context, state->sprite_material,
			0, 0, sizeof(vec4), &sprite_color, true);
		vulkan_renderer_resource_group_set_texture_sampler(context->vk_context,
			state->sprite_material, 0, state->sprite_texture, RENDERER_DEFAULT_SAMPLER_NEAREST);

		state->is_initialized = true;
	}

	// Render the sprite at origin
	mat4 transform = GLM_MAT4_IDENTITY_INIT;
	glm_translate(transform, (vec3){ 0.0f, 1.0f, 0.0f });
	glm_scale(transform, (vec3){
						   2.0f,
						   2.0f,
						   2.0f,
						 });

	vulkan_renderer_shader_bind(
		context->vk_context, state->sprite_shader,
		RENDERER_DEFAULT_SHADER_VARIANT_STANDARD);

	vulkan_renderer_resource_group_write(
		context->vk_context, state->sprite_material,
		0, 0, sizeof(vec4),
		(vec4){ 1.0f, 1.0f, 1.0f, 1.0f }, false);

	vulkan_renderer_resource_group_bind(context->vk_context, state->sprite_material, 0);
	vulkan_renderer_resource_local_write(context->vk_context, 0, sizeof(mat4), transform);
	vulkan_renderer_buffer_bind(context->vk_context, state->quad_vb, 0);
	vulkan_renderer_draw(context->vk_context, state->quad_vertex_count);

	return true;
}

bool game_on_unload(GameContext *context) {
	LOG_INFO("Game unloading...");
	return true;
}

GameInterface *game_hookup(void) {
	interface = (GameInterface){
		.on_load = game_on_load,
		.on_update = game_on_update,
		.on_unload = game_on_unload,
	};
	return &interface;
}

UUID create_plane_mesh(Arena *arena, uint32_t subdivide_width, uint32_t subdivide_depth, MeshSource **out_mesh) {
	uint32_t rows = subdivide_width + 1;
	uint32_t columns = subdivide_depth + 1;

	UUID id = identifier_create_from_u64(12345); // Simple ID for now

	(*out_mesh) = arena_push_struct(arena, MeshSource);
	(*out_mesh)->id = id;
	(*out_mesh)->vertices = arena_push_array_zero(arena, Vertex, 6);
	(*out_mesh)->vertex_count = 6;

	// Create a simple quad facing +Z (toward camera)
	Vertex v0 = {
		.position = { -0.5f, 0.5f, 0.0f },
		.normal = { 0.0f, 0.0f, 1.0f },
		.uv = { 0.0f, 0.0f },
		.tangent = { 1.0f, 0.0f, 0.0f, 1.0f }
	};
	Vertex v1 = {
		.position = { -0.5f, -0.5f, 0.0f },
		.normal = { 0.0f, 0.0f, 1.0f },
		.uv = { 0.0f, 1.0f },
		.tangent = { 1.0f, 0.0f, 0.0f, 1.0f }
	};
	Vertex v2 = {
		.position = { 0.5f, -0.5f, 0.0f },
		.normal = { 0.0f, 0.0f, 1.0f },
		.uv = { 1.0f, 1.0f },
		.tangent = { 1.0f, 0.0f, 0.0f, 1.0f }
	};
	Vertex v3 = {
		.position = { 0.5f, 0.5f, 0.0f },
		.normal = { 0.0f, 0.0f, 1.0f },
		.uv = { 1.0f, 0.0f },
		.tangent = { 1.0f, 0.0f, 0.0f, 1.0f }
	};

	// Triangle 1
	(*out_mesh)->vertices[0] = v0;
	(*out_mesh)->vertices[1] = v1;
	(*out_mesh)->vertices[2] = v2;

	// Triangle 2
	(*out_mesh)->vertices[3] = v0;
	(*out_mesh)->vertices[4] = v2;
	(*out_mesh)->vertices[5] = v3;

	(*out_mesh)->indices = NULL;
	(*out_mesh)->index_size = 0;
	(*out_mesh)->index_count = 0;
	(*out_mesh)->material = NULL;

	return id;
}
