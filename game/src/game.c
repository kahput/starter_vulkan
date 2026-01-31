#include "input.h"
#include "mesh_source.h"

#include <cglm/vec3-ext.h>
#include <cglm/vec3.h>
#include <game_interface.h>

#include <common.h>
#include <core/arena.h>
#include <core/debug.h>
#include <core/logger.h>
#include <core/astring.h>

#include <pthread.h>
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

	vec3 player_position;
	Camera camera;

	// Texture
	RhiTexture sprite_texture;
	RhiTexture checkered_texture;

	uint32_t variant_index;

	bool is_initialized;
} GameState;

static GameState *state = NULL;

RhiShader create_shader(GameContext *context, String filename);
RhiTexture create_texture(GameContext *context, String filename);

void player_update(vec3 player_position, float dt, Camera *camera);

#define CAMERA_SENSITIVITY .001f
#define MOVE_SPEED 5.f
#define SPRING_LENGTH 16.f

bool game_on_load(GameContext *context) {
	LOG_INFO("Game loading...");
	LOG_INFO("Game loaded successfully");
	return true;
}

FrameInfo game_on_update(GameContext *context, float dt) {
	state = (GameState *)context->permanent_memory;

	ArenaTemp scratch = arena_scratch(NULL);
	if (state->is_initialized == false) {
		state->arena = (Arena){
			.memory = state + 1,
			.offset = 0,
			.capacity = context->permanent_memory_size - sizeof(GameState)
		};

		state->current_frame = 0;
		for (uint32_t index = 0; index < countof(state->terrain); ++index)
			state->terrain[index].vb.id = INVALID_INDEX;

		state->sprite_shader = create_shader(context, str_lit("sprite.glsl"));
		state->terrain_shader = create_shader(context, str_lit("terrain.glsl"));

		MeshSource plane_src = mesh_source_cube_face_create(scratch.arena, 0, 0, 0, CUBE_FACE_FRONT);

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

		float azimuth = GLM_PIf * 3 / 2.f;
		float thetha = GLM_PIf / 3.f;

		state->camera = (Camera){
			.position = {
			  (SPRING_LENGTH * sinf(thetha) * cosf(azimuth)) + state->player_position[0],
			  SPRING_LENGTH * cosf(thetha),
			  (SPRING_LENGTH * sinf(thetha) * sinf(azimuth)) + state->player_position[2],
			},
			.target = { 0.0f, 0.0f, 0.0f },
			.up = { 0.0f, 1.0f, 0.0f },
			.fov = 45.f,
			.projection = CAMERA_PROJECTION_PERSPECTIVE
		};

		state->is_initialized = true;
	}

	Terrain *terrain = &state->terrain[state->current_frame];

	MeshList list = { 0 };

	uint32_t size = 32;
	for (uint32_t z = 0; z < size; ++z) {
		for (uint32_t x = 0; x < size; x++) {
			for (int32_t face_index = 0; face_index < 6; ++face_index) {
				float x_offset = (float)x - ((float)size * .5f);
				float z_offset = (float)z - ((float)size * .5f);

				MeshSource source = mesh_source_cube_face_create(
					scratch.arena, x_offset, -1.f, z_offset, face_index);
				mesh_source_list_push(scratch.arena, &list, source);

				if (x == 0 || x + 1 == size || z == 0 || z + 1 == size) {
					for (uint32_t y = 0; y < 3; ++y) {
						MeshSource wall = mesh_source_cube_face_create(
							scratch.arena, x_offset, y, z_offset, face_index);
						mesh_source_list_push(scratch.arena, &list, wall);
					}
				}
			}
		}
	}
	MeshSource cube_src = mesh_source_list_flatten(scratch.arena, &list);

	if (terrain->vb.id != INVALID_INDEX || terrain->vertex_count < cube_src.vertex_count) {
		vulkan_renderer_buffer_destroy(context->vk_context, terrain->vb);
		terrain->vb.id = INVALID_INDEX;
		terrain->vb = vulkan_renderer_buffer_create(context->vk_context, BUFFER_TYPE_VERTEX,
			cube_src.vertex_size * cube_src.vertex_count, cube_src.vertices);
	} else
		vulkan_renderer_buffer_write(context->vk_context, terrain->vb,
			0, cube_src.vertex_size * cube_src.vertex_count, cube_src.vertices);

	terrain->vertex_count = cube_src.vertex_count;

	mat4 transform = GLM_MAT4_IDENTITY_INIT;
	glm_translate(transform, state->player_position);

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

	if (input_key_pressed(KEY_CODE_ENTER))
		state->variant_index = !state->variant_index;

	player_update(state->player_position, dt, &state->camera);

	FrameInfo info = {
		.camera = state->camera,
	};

	return info;
}

bool game_on_unload(GameContext *context) {
	LOG_INFO("Game unloading...");
	return true;
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

void player_update(vec3 player_position, float dt, Camera *camera) {
	static float azimuth = GLM_PIf * 3 / 2.f;
	static float theta = GLM_PIf / 3.f;

	float yaw_delta = input_mouse_dx() * CAMERA_SENSITIVITY;
	float pitch_delta = input_mouse_dy() * CAMERA_SENSITIVITY;

	azimuth = fmodf(azimuth + yaw_delta, GLM_PI * 2.f);
	if (azimuth < 0)
		azimuth += GLM_PI * 2.f;

	theta = clamp(theta - pitch_delta, GLM_PIf / 4.f, GLM_PIf / 2.f);

	vec3 offset;
	glm_vec3_sub(camera->position, player_position, offset);

	float r = glm_vec3_norm(offset);
	if (r < 1e-6f)
		r = 1e-6f;

	float current_theta = acosf(offset[1] / r);
	float current_azimuth = atan2f(offset[2], offset[0]); // [-pi, pi]

	if (current_azimuth < 0)
		current_azimuth += GLM_PI * 2.f;

	float da = azimuth - current_azimuth;
	if (da > GLM_PI)
		da -= GLM_PI * 2.f;
	if (da < -GLM_PI)
		da += GLM_PI * 2.f;

	float lerp = 5.0f * dt;

	current_azimuth += lerp * da;
	current_theta += lerp * (theta - current_theta);

	LOG_INFO("a - ca = %.2f", lerp * (azimuth - current_azimuth));

	glm_vec3_copy(
		(vec3){
		  (SPRING_LENGTH * sinf(current_theta) * cosf(current_azimuth)) + player_position[0],
		  SPRING_LENGTH * cosf(current_theta),
		  (SPRING_LENGTH * sinf(current_theta) * sinf(current_azimuth)) + player_position[2],
		},
		camera->position);
	LOG_INFO("Position after = { %.2f, %.2f, %.2f }",

		camera->position[0], camera->position[1], camera->position[2]);
	vec3 camera_position, camera_target;
	glm_vec3_copy(camera->position, camera_position);
	camera_position[1] = 0.0f;
	glm_vec3_copy(camera->target, camera_target);
	camera_target[1] = 0.0f;

	vec3 forward, right;

	glm_vec3_sub(
		camera_target,
		camera_position, forward);
	glm_vec3_normalize(forward);
	glm_vec3_cross(forward, camera->up, right);
	glm_vec3_normalize(right);

	vec3 direction = { 0, 0, 0 };
	glm_vec3_muladds(forward, (input_key_down(KEY_CODE_W) - input_key_down(KEY_CODE_S)), direction);
	glm_vec3_muladds(right, (input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A)), direction);
	if (glm_vec3_norm(direction) > 1e-6f)
		glm_vec3_normalize(direction);

	glm_vec3_muladds(direction, MOVE_SPEED * dt, player_position);
	camera->target[0] = player_position[0];
	camera->target[2] = player_position[2];
}

GameInterface *game_hookup(void) {
	interface = (GameInterface){
		.on_load = game_on_load,
		.on_update = game_on_update,
		.on_unload = game_on_unload,
	};
	return &interface;
}
