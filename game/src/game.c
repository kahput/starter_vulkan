#include "input.h"
#include "mesh_source.h"

#include <game_interface.h>

#include <common.h>
#include <core/arena.h>
#include <core/debug.h>
#include <core/logger.h>
#include <core/astring.h>

#include <renderer.h>
#include <renderer/r_internal.h>
#include <renderer/backend/vulkan_api.h>

#include <assets.h>
#include <assets/asset_types.h>

#include <cglm/cglm.h>
#include <cglm/mat4.h>
#include <string.h>

static GameInterface interface;

typedef struct {
	RhiBuffer vb;
	uint32_t vertex_count;
} Terrain;

typedef struct {
	Arena arena;
	// Sprite shader
	RhiShader sprite_shader;
	RhiGroupResource sprite_material;

	RhiShader terrain_shader;
	RhiGroupResource terrain_material;

	// Quad mesh
	RhiBuffer quad_vb;
	uint32_t quad_vertex_count;

	Terrain terrain[2];

	uint32_t current_frame;

	// Texture
	RhiTexture sprite_texture;
	RhiTexture checkered_texture;

	uint32_t variant_index;

	bool is_initialized;
} GameState;

static GameState *state = NULL;

typedef enum {
	RIGHT,
	LEFT,
	TOP,
	BOTTOM,
	FRONT,
	BACK
} Orientation;

MeshSource create_quad_mesh(Arena *arena, float x, float y, float z, Orientation orientation);
RhiShader create_shader(GameContext *context, String filename);
RhiTexture create_texture(GameContext *context, String filename);

bool game_on_load(GameContext *context) {
	LOG_INFO("Game loading...");
	LOG_INFO("Game loaded successfully");
	return true;
}

bool game_on_update(GameContext *context, float dt) {
	state = (GameState *)context->game_memory;

	ArenaTemp scratch = arena_scratch(NULL);
	if (state->is_initialized == false) {
		state->arena = (Arena){
			.memory = state + 1,
			.offset = 0,
			.capacity = context->game_memory_size - sizeof(GameState)
		};

		state->current_frame = 0;
		for (uint32_t index = 0; index < countof(state->terrain); ++index)
			state->terrain[index].vb.id = INVALID_INDEX;

		state->sprite_shader = create_shader(context, str_lit("sprite.glsl"));
		state->terrain_shader = create_shader(context, str_lit("terrain.glsl"));

		MeshSource plane_src = create_quad_mesh(scratch.arena, 0, 0, 0, FRONT);

		state->quad_vertex_count = plane_src.vertex_count;

		state->quad_vb = vulkan_renderer_buffer_create(context->vk_context, BUFFER_TYPE_VERTEX,
			plane_src.vertex_size * plane_src.vertex_count, plane_src.vertices);

		// Load sprite texture
		state->sprite_texture = create_texture(context, str_lit("tile_0085.png"));
		state->checkered_texture = create_texture(context, str_lit("texture_09.png"));

		// Create material
		vec4 sprite_tint = { 1.0f, 1.0f, 1.0f, 1.0f };

		state->sprite_material = vulkan_renderer_resource_group_create(context->vk_context,
			state->sprite_shader, 1);

		vulkan_renderer_resource_group_write(context->vk_context, state->sprite_material,
			0, 0, sizeof(vec4), &sprite_tint, true);

		vulkan_renderer_resource_group_set_texture_sampler(context->vk_context,
			state->sprite_material, 0, state->sprite_texture, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_NEAREST, 0 });

		vec4 terrain_tint = { 1.0f, 1.0f, 1.0f, 1.0f };

		state->terrain_material = vulkan_renderer_resource_group_create(context->vk_context,
			state->terrain_shader, 1);

		vulkan_renderer_resource_group_write(
			context->vk_context, state->terrain_material,
			0, 0, sizeof(vec4), &terrain_tint, true);

		vulkan_renderer_resource_group_set_texture_sampler(context->vk_context,
			state->terrain_material, 0, state->checkered_texture, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR, 0 });

		state->is_initialized = true;
	}

	Terrain *terrain = &state->terrain[state->current_frame];
	if (terrain->vb.id != INVALID_INDEX) {
		vulkan_renderer_buffer_destroy(context->vk_context, terrain->vb);
		terrain->vb.id = INVALID_INDEX;
	}

	MeshList list = { 0 };

	uint32_t size = 128;
	for (uint32_t z = 0; z < size; ++z) {
		for (uint32_t x = 0; x < size; x++) {
			for (int32_t face_index = TOP; face_index <= TOP; ++face_index) {
				float x_offset = (float)x - ((float)size * .5f);
				float z_offset = (float)z - ((float)size * .5f);

				MeshSource source = create_quad_mesh(
					scratch.arena, x_offset, -1.f, z_offset, face_index);
				mesh_source_list_push(scratch.arena, &list, source);
			}
		}
	}
	MeshSource cube_src = mesh_source_list_flatten(scratch.arena, &list);
	terrain->vertex_count = cube_src.vertex_count;

	terrain->vb = vulkan_renderer_buffer_create(context->vk_context, BUFFER_TYPE_VERTEX,
		cube_src.vertex_size * cube_src.vertex_count, cube_src.vertices);

	// Render the sprite at origin
	mat4 transform = GLM_MAT4_IDENTITY_INIT;
	glm_scale(transform, (vec3){ 1.0f, 1.0f, 1.0f });

	vulkan_renderer_shader_bind(
		context->vk_context, state->sprite_shader,
		RENDERER_DEFAULT_SHADER_VARIANT_STANDARD);

	vulkan_renderer_resource_group_write(
		context->vk_context, state->sprite_material,
		0, 0, sizeof(vec4),
		(vec4){ 1.0f, 1.0f, 1.0f, 1.0f }, false);

	vulkan_renderer_resource_group_write(
		context->vk_context, state->terrain_material,
		0, 0, sizeof(vec4),
		(vec4){ 1.0f, 1.0f, 1.0f, 1.0f }, false);

	vulkan_renderer_resource_group_bind(context->vk_context, state->sprite_material, 0);
	vulkan_renderer_resource_local_write(context->vk_context, 0, sizeof(mat4), transform);
	vulkan_renderer_buffer_bind(context->vk_context, state->quad_vb, 0);
	vulkan_renderer_draw(context->vk_context, state->quad_vertex_count);

	glm_mat4_identity(transform);

	vulkan_renderer_shader_bind(
		context->vk_context, state->terrain_shader,
		state->variant_index);
	vulkan_renderer_resource_group_bind(context->vk_context, state->terrain_material, 0);

	vulkan_renderer_resource_local_write(context->vk_context, 0, sizeof(mat4), transform);
	vulkan_renderer_buffer_bind(context->vk_context, terrain->vb, 0);
	vulkan_renderer_draw(context->vk_context, terrain->vertex_count);

	arena_release_scratch(scratch);
	state->current_frame = (state->current_frame + 1) % 2;

	if (input_key_pressed(KEY_CODE_SPACE))
		state->variant_index = !state->variant_index;

	return true;
}

bool game_on_unload(GameContext *context) {
	LOG_INFO("Game unloading...");
	return true;
}

MeshSource create_quad_mesh(Arena *arena, float x, float y, float z, Orientation orientation) {
	MeshSource rv = {
		.vertex_size = sizeof(Vertex),
		.vertex_count = 6,
	};

	Vertex *vertices = arena_push_array_zero(arena, Vertex, rv.vertex_count);
	rv.vertices = (uint8_t *)vertices;

	/**
				5---4
			   /|  /|
			  0---1 |
			  | 7-|-6
			  |/  |/
			  2---3
	**/

	const vec3 positions[8] = {
		[0] = { x + -0.5f, y + 0.5f, z + 0.5f },
		[1] = { x + 0.5f, y + 0.5f, z + 0.5f },
		[2] = { x + -0.5f, y + -0.5f, z + 0.5f },
		[3] = { x + 0.5f, y + -0.5f, z + 0.5f },
		[4] = { x + 0.5f, y + 0.5f, z + -0.5f },
		[5] = { x + -0.5f, y + 0.5f, z + -0.5f },
		[6] = { x + 0.5f, y + -0.5f, z + -0.5f },
		[7] = { x + -0.5f, y + -0.5f, z + -0.5f }
	};

	const vec3 normals[6] = {
		[RIGHT] = { 1.0f, 0.0f, 0.0f },
		[LEFT] = { -1.0f, 0.0f, 0.0f },
		[TOP] = { 0.0f, 1.0f, 0.0f },
		[BOTTOM] = { 0.0f, -1.0f, 0.0f },
		[FRONT] = { 0.0f, 0.0f, 1.0f },
		[BACK] = { 0.0f, 0.0f, -1.0f }
	};

	const vec2 uvs[6] = {
		{ 0.0f, 0.0f },
		{ 0.0f, 1.0f },
		{ 1.0f, 0.0f },
		{ 1.0f, 0.0f },
		{ 0.0f, 1.0f },
		{ 1.0f, 1.0f }
	};

	const uint8_t indices[6][6] = {
		[RIGHT] = { 1, 3, 4, 4, 3, 6 },
		[LEFT] = { 5, 7, 0, 0, 7, 2 },
		[TOP] = { 1, 4, 0, 0, 4, 5 },
		[BOTTOM] = { 2, 7, 3, 3, 7, 6 },
		[FRONT] = { 0, 2, 1, 1, 2, 3 },
		[BACK] = { 4, 6, 5, 5, 6, 7 }
	};

	for (int face_index = 0; face_index < 6; face_index++) {
		int index = indices[orientation][face_index];

		memcpy(vertices[face_index].position, positions[index], sizeof(vec3));
		memcpy(vertices[face_index].normal, normals[orientation], sizeof(vec3));
		memcpy(vertices[face_index].uv, uvs[face_index], sizeof(vec2));
	}

	return rv;
}

RhiShader create_shader(GameContext *context, String filename) {
	// Load sprite shader
	ShaderSource *shader_src = NULL;
	asset_library_request_shader(context->asset_library, filename, &shader_src);
	if (shader_src == NULL) {
		LOG_ERROR("Failed to load %.*s shader", str_expand(filename));
		ASSERT(false);
		return (RhiShader){ 0 };
	}

	ShaderConfig config = {
		.vertex_code = shader_src->vertex_shader.content,
		.vertex_code_size = shader_src->vertex_shader.size,
		.fragment_code = shader_src->fragment_shader.content,
		.fragment_code_size = shader_src->fragment_shader.size,
	};

	PipelineDesc desc = DEFAULT_PIPELINE();
	desc.cull_mode = CULL_MODE_BACK;
	desc.front_face = FRONT_FACE_COUNTER_CLOCKWISE;

	ShaderReflection reflection;

	RhiShader shader = vulkan_renderer_shader_create(&state->arena, context->vk_context,
		(RhiGlobalResource){ RENDERER_GLOBAL_RESOURCE_MAIN, 0 }, &config, &reflection);

	vulkan_renderer_shader_variant_create(context->vk_context, shader,
		(RhiPass){ RENDERER_DEFAULT_PASS_MAIN, 0 }, desc);

	desc.polygon_mode = POLYGON_MODE_LINE;
	vulkan_renderer_shader_variant_create(context->vk_context, shader,
		(RhiPass){ RENDERER_DEFAULT_PASS_MAIN, 0 }, desc);

	return shader;
}

RhiTexture create_texture(GameContext *context, String filename) {
	ImageSource *texture_src = NULL;
	UUID texture_id = asset_library_request_image(context->asset_library, filename, &texture_src);

	if (!texture_src) {
		LOG_ERROR("Failed to load texture texture");
		return (RhiTexture){ 0 };
	}

	RhiTexture texture = vulkan_renderer_texture_create(context->vk_context,
		texture_src->width, texture_src->height,
		TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB,
		TEXTURE_USAGE_SAMPLED, texture_src->pixels);

	return texture;
}

GameInterface *game_hookup(void) {
	interface = (GameInterface){
		.on_load = game_on_load,
		.on_update = game_on_update,
		.on_unload = game_on_unload,
	};
	return &interface;
}
