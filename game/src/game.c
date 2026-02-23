#include "input.h"
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
	RMesh *meshes;
	uint32_t mesh_count;

	RMaterial *materials;
	uint32_t material_count;

	Matrix4f transform;
} GameModel;

typedef struct {
	Arena arena;
	// Sprite shader
	RhiShader sprite_shader;
	RhiBuffer sprite_uniform_buffer;
	RhiUniformSet sprite_material;

	RhiShader model_shader;
	RhiBuffer model_uniform_buffer;
	RhiUniformSet model_material;

	RhiShader terrain_shader;
	RhiBuffer terrain_uniform_buffer;
	RhiUniformSet terrain_material;

	// Quad mesh
	RhiBuffer quad_vb;
	uint32_t quad_vertex_count;

	GameModel barrel;
	GameModel crate;

	GameModel walls[3];
	RMesh terrain[2];

	uint32_t current_frame;

	float3 player_position; // Changed from vector3
	Camera camera;

	// Texture
	RhiTexture sprite_texture;
	RhiTexture checkered_texture;

	PipelineDesc pipeline_desc;

	bool is_initialized;
} GameState;

static GameState *state = NULL;

GameModel load_game_model(GameContext *context, String file);
RhiShader create_shader(GameContext *context, String filename);
RhiTexture create_texture(GameContext *context, String filename);

// Changed signature: passing pointer to float3 to allow modification
void player_update(float3 *player_position, float dt, Camera *camera);

#define CAMERA_SENSITIVITY .001f
#define MOVE_SPEED 5.f
#define SPRING_LENGTH 16.f

FrameInfo game_on_update_and_render(GameContext *context, float dt) {
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
			state->terrain[index].vb.id = 0;

		/*
		 * ///////////////////////////////////////////////////////////////////////////////////////////
		 * /// INITIALIZATION
		 * ///////////////////////////////////////////////////////////////////////////////////////////
		 *
		 * UUID sprite_shader = asset_library_request_shader(context->asset_library, str_lit("sprite.glsl"));
		 * UUID sprite = asset_library_request_image(context->asset_library, str_lit("tile_0085.png"));
		 *
		 * ArenaTemp scratch = arena_scratch();
		 * MeshSource plane = mesh_source_plane_create(scratch.arena, 0, 0, 0, PLANE_Z);
		 *
		 * // Unsure about materials
		 *
		 * MaterialSource sprite_mat_src = {
		 *     .shader = sprite_shader,
		 *     .properties = {
		 *         [0] = { .name = str_lit("u_tint"), FORMAT_FLOAT_4, .as.float4 = {1.0f, 1.0f, 1.0f, 1.0f}},
		 *     }
		 * }
		 * UUID sprite_mat_base = asset_library_register_material(context->asset_library, str_lit("sprite.mat"), sprite_mat_src);
		 *
		 * ///////////////////////////////////////////////////////////////////////////////////////////
		 * /// Drawing
		 * ///////////////////////////////////////////////////////////////////////////////////////////
		 *
		 * MaterialProperty overrides[] = {
		 *     [0] = {.name = str_lit("u_tint"), FORMAT_FLOAT_4, .as.float4 = { 1.0f, 0.0f, 1.0f, 1.0f },
		 * }
		 *
		 * typedef struct draw_packet {
		 *     UUID base_material;
         *     ...
         *
		 *     MaterialProperty *overrides;
		 *     uint32_t override_count;
		 * } DrawPacket;
		 *
		 *
		 * draw_list_push_generated(context, plane, ...);
		 * // draw_list_push_model(context, model, ...);
		 *
		 *
		 */

		state->sprite_shader = create_shader(context, str_lit("sprite.glsl"));
		state->terrain_shader = create_shader(context, str_lit("terrain.glsl"));
		state->model_shader = create_shader(context, str_lit("pbr.glsl"));

		state->pipeline_desc = DEFAULT_PIPELINE;

		MeshSource plane_src = mesh_source_cube_face_create(scratch.arena, 0, 0, 0, CUBE_FACE_FRONT);

		state->barrel = load_game_model(context, str_lit("barrel.glb"));
		state->crate = load_game_model(context, str_lit("crate.glb"));

		/* SModel walls_door = { 0 }; */
		/* asset_library_load_model(scratch.arena, context->asset_library, str_lit("walls_door.glb"), &walls_door, false); */

		/* MeshSource wall_mesh = walls_door.meshes[0]; */
		/* state->walls[0].vb = vulkan_renderer_buffer_create( */
		/* 	context->vk_context, BUFFER_TYPE_VERTEX, */
		/* 	wall_mesh.vertex_count * sizeof(Vertex), wall_mesh.vertices); */

		/* state->walls[0].ib = vulkan_renderer_buffer_create( */
		/* 	context->vk_context, BUFFER_TYPE_INDEX, */
		/* 	wall_mesh.index_size * wall_mesh.index_count, wall_mesh.indices); */

		/* state->walls[0].vertex_count = wall_mesh.vertex_count; */
		/* state->walls[0].index_count = wall_mesh.index_count; */
		/* state->walls[0].index_size = wall_mesh.index_size; */

		state->quad_vertex_count = plane_src.vertex_count;

		state->quad_vb = vulkan_renderer_buffer_create(context->vk_context, BUFFER_TYPE_VERTEX,
			plane_src.vertex_size * plane_src.vertex_count, plane_src.vertices);

		// Load sprite texture
		state->sprite_texture = create_texture(context, str_lit("tile_0085.png"));
		state->checkered_texture = create_texture(context, str_lit("texture_09.png"));

		// Create material
		float4 sprite_tint = { 1.0f, 1.0f, 1.0f, 1.0f };

		state->sprite_material =
			vulkan_renderer_uniform_set_create(context->vk_context, state->sprite_shader, 1);

		state->sprite_uniform_buffer = vulkan_renderer_buffer_create(context->vk_context, BUFFER_TYPE_UNIFORM, sizeof(float4), &sprite_tint);
		vulkan_renderer_uniform_set_bind_buffer(context->vk_context, state->sprite_material, 1, state->sprite_uniform_buffer);

		vulkan_renderer_uniform_set_bind_texture(context->vk_context,
			state->sprite_material, 0, state->sprite_texture, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_NEAREST });

		state->model_material =
			vulkan_renderer_uniform_set_create(context->vk_context, state->model_shader, 1);
		MaterialParameters default_material_parameters = {
			.base_color_factor = { 0.8f, 0.8f, 0.8f, 1.0f },
			.emissive_factor = { 0.0f, 0.0f, 0.0f, 0.0f },
			.roughness_factor = 0.5f,
			.metallic_factor = 0.0f,
		};

		state->model_uniform_buffer = vulkan_renderer_buffer_create(context->vk_context, BUFFER_TYPE_UNIFORM, sizeof(MaterialParameters), &default_material_parameters);
		vulkan_renderer_uniform_set_bind_buffer(context->vk_context, state->model_material, 5, state->model_uniform_buffer);

		vulkan_renderer_uniform_set_bind_texture(context->vk_context, state->model_material, 0, (RhiTexture){ RENDERER_DEFAULT_TEXTURE_WHITE }, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR });
		vulkan_renderer_uniform_set_bind_texture(context->vk_context, state->model_material, 1, (RhiTexture){ RENDERER_DEFAULT_TEXTURE_WHITE }, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR });
		vulkan_renderer_uniform_set_bind_texture(context->vk_context, state->model_material, 2, (RhiTexture){ RENDERER_DEFAULT_TEXTURE_NORMAL }, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR });
		vulkan_renderer_uniform_set_bind_texture(context->vk_context, state->model_material, 3, (RhiTexture){ RENDERER_DEFAULT_TEXTURE_WHITE }, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR });
		vulkan_renderer_uniform_set_bind_texture(context->vk_context, state->model_material, 4, (RhiTexture){ RENDERER_DEFAULT_TEXTURE_BLACK }, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR });

		float4 terrain_tint = { 1.0f, 1.0f, 1.0f, 1.0f };

		state->terrain_material = vulkan_renderer_uniform_set_create(context->vk_context,
			state->terrain_shader, 1);

		state->terrain_uniform_buffer = vulkan_renderer_buffer_create(context->vk_context, BUFFER_TYPE_UNIFORM, sizeof(float4), &terrain_tint);
		vulkan_renderer_uniform_set_bind_buffer(context->vk_context, state->terrain_material, 1, state->terrain_uniform_buffer);

		vulkan_renderer_uniform_set_bind_texture(context->vk_context,
			state->terrain_material, 0, state->checkered_texture, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR });

		float azimuth = C_PIf * 3 / 2.f;
		float thetha = C_PIf / 3.f;

		state->camera = (Camera){
			.position = {
			  (SPRING_LENGTH * sinf(thetha) * cosf(azimuth)) + state->player_position.x,
			  SPRING_LENGTH * cosf(thetha),
			  (SPRING_LENGTH * sinf(thetha) * sinf(azimuth)) + state->player_position.z,
			},
			.target = { 0.0f, 0.0f, 0.0f },
			.up = { 0.0f, 1.0f, 0.0f },
			.fov = 45.f,
			.projection = CAMERA_PROJECTION_PERSPECTIVE
		};

		state->is_initialized = true;
	}

	RMesh *terrain = &state->terrain[state->current_frame];
	MeshSourceList list = { 0 };

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

	if (terrain->vb.id || terrain->vertex_count < cube_src.vertex_count) {
		vulkan_renderer_buffer_destroy(context->vk_context, terrain->vb);
		terrain->vb.id = 0;
		terrain->vb = vulkan_renderer_buffer_create(context->vk_context, BUFFER_TYPE_VERTEX,
			cube_src.vertex_size * cube_src.vertex_count, cube_src.vertices);
	} else
		vulkan_renderer_buffer_write(context->vk_context, terrain->vb,
			0, cube_src.vertex_size * cube_src.vertex_count, cube_src.vertices);

	terrain->vertex_count = cube_src.vertex_count;

	Matrix4f transform = matrix4f_translated(state->player_position);

	vulkan_renderer_buffer_write(
		context->vk_context, state->sprite_uniform_buffer,
		0, sizeof(float4), &(float4){ 1.0f, 1.0f, 1.0f, 1.0f });

	vulkan_renderer_buffer_write(
		context->vk_context, state->terrain_uniform_buffer,
		0, sizeof(float4), &(float4){ 1.0f, 1.0f, 1.0f, 1.0f });

	vulkan_renderer_shader_bind(
		context->vk_context, state->sprite_shader,
		state->pipeline_desc);

	vulkan_renderer_uniform_set_bind(context->vk_context, state->sprite_material);
	vulkan_renderer_push_constants(context->vk_context, 0, sizeof(Matrix4f), &transform);
	vulkan_renderer_buffer_bind(context->vk_context, state->quad_vb, 0);
	vulkan_renderer_draw(context->vk_context, state->quad_vertex_count);

	vulkan_renderer_shader_bind(context->vk_context, state->model_shader, state->pipeline_desc);

	for (uint32_t index = 0; index < state->barrel.mesh_count; ++index) {
		RMesh *mesh = &state->barrel.meshes[index];
		RMaterial *material = &state->barrel.materials[index];

		transform = matrix4f_translate(matrix4f_identity(), (Vector3f){ -3.0f, 0.0f, 0.0f });
		vulkan_renderer_push_constants(context->vk_context, 0, sizeof(Matrix4f), &transform);

		vulkan_renderer_uniform_set_bind(context->vk_context, material->set);
		vulkan_renderer_buffer_bind(context->vk_context, mesh->vb, 0);
		vulkan_renderer_buffer_bind(context->vk_context, mesh->ib, mesh->index_size);
		vulkan_renderer_draw_indexed(context->vk_context, mesh->index_count);
	}

	for (uint32_t index = 0; index < state->crate.mesh_count; ++index) {
		RMesh *mesh = &state->crate.meshes[index];
		RMaterial *material = &state->crate.materials[index];

		transform = matrix4f_translate(matrix4f_identity(), (Vector3f){ 3.0f, 0.0f, 0.0f });
		vulkan_renderer_push_constants(context->vk_context, 0, sizeof(Matrix4f), &transform);

		vulkan_renderer_uniform_set_bind(context->vk_context, material->set);
		vulkan_renderer_buffer_bind(context->vk_context, mesh->vb, 0);
		vulkan_renderer_buffer_bind(context->vk_context, mesh->ib, mesh->index_size);
		vulkan_renderer_draw_indexed(context->vk_context, mesh->index_count);
	}

	vulkan_renderer_uniform_set_bind(context->vk_context, state->model_material);

	/* transform = matrix4f_translate(matrix4f_identity(), (Vector3f){ 0.0f, 0.0f, 3.0f }); */
	/* vulkan_renderer_buffer_bind(context->vk_context, state->walls[0].vb, 0); */
	/* vulkan_renderer_buffer_bind(context->vk_context, state->walls[0].ib, state->walls[0].index_size); */
	/* vulkan_renderer_push_constants(context->vk_context, 0, sizeof(Matrix4f), &transform); */
	/* vulkan_renderer_draw_indexed(context->vk_context, state->walls[0].index_count); */

	transform = matrix4f_identity();

	vulkan_renderer_shader_bind(
		context->vk_context, state->terrain_shader,
		state->pipeline_desc);
	vulkan_renderer_uniform_set_bind(context->vk_context, state->terrain_material);

	vulkan_renderer_push_constants(context->vk_context, 0, sizeof(Matrix4f), &transform);
	vulkan_renderer_buffer_bind(context->vk_context, terrain->vb, 0);
	vulkan_renderer_draw(context->vk_context, terrain->vertex_count);

	arena_release_scratch(scratch);
	state->current_frame = (state->current_frame + 1) % 2;

	if (input_key_pressed(KEY_CODE_ENTER))
		state->pipeline_desc.polygon_mode = !state->pipeline_desc.polygon_mode;
	if (input_key_pressed(KEY_CODE_P))
		state->pipeline_desc.front_face = !state->pipeline_desc.front_face;

	player_update(&state->player_position, dt, &state->camera);

	FrameInfo info = {
		.camera = state->camera,
	};

	return info;
}

RhiShader create_shader(GameContext *context, String filename) {
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

	ShaderReflection reflection;

	RhiShader shader = vulkan_renderer_shader_create(
		&state->arena, context->vk_context, &config, &reflection);

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

GameModel load_game_model(GameContext *context, String file) {
	GameModel result = { 0 };

	ArenaTemp scratch = arena_scratch(NULL);

	SModel src = { 0 };
	asset_library_load_model(scratch.arena, context->asset_library, file, &src, false);

	result.mesh_count = src.mesh_count;
	result.material_count = src.material_count;

	result.meshes = arena_push_array(&state->arena, RMesh, result.mesh_count);
	result.materials = arena_push_array(&state->arena, RMaterial, result.mesh_count);

	for (uint32_t index = 0; index < src.mesh_count; ++index) {
		MeshSource *src_mesh = &src.meshes[index];
		MaterialSource *src_material = &src.materials[src.mesh_to_material[index]];

		RMesh *dst_mesh = &result.meshes[index];
		RMaterial *dst_material = &result.materials[index];

		dst_mesh->vb = vulkan_renderer_buffer_create(
			context->vk_context, BUFFER_TYPE_VERTEX,
			src_mesh->vertex_count * sizeof(Vertex), src_mesh->vertices);

		dst_mesh->ib = vulkan_renderer_buffer_create(
			context->vk_context, BUFFER_TYPE_INDEX,
			src_mesh->index_size * src_mesh->index_count, src_mesh->indices);

		dst_mesh->vertex_count = src_mesh->vertex_count;
		dst_mesh->index_count = src_mesh->index_count;
		dst_mesh->index_size = src_mesh->index_size;

		dst_material->set =
			vulkan_renderer_uniform_set_create(context->vk_context, state->model_shader, 1);
		MaterialParameters default_material_parameters = {
			.base_color_factor = src_material->properties[5].as.float4,
			.emissive_factor = src_material->properties[6].as.float4,
			.roughness_factor = src_material->properties[7].as.float1,
			.metallic_factor = src_material->properties[8].as.float1,
		};

		dst_material->ubo = vulkan_renderer_buffer_create(context->vk_context, BUFFER_TYPE_UNIFORM, sizeof(MaterialParameters), &default_material_parameters);
		vulkan_renderer_uniform_set_bind_buffer(context->vk_context, dst_material->set, 5, dst_material->ubo);

		vulkan_renderer_uniform_set_bind_texture(context->vk_context, dst_material->set, 0, (RhiTexture){ RENDERER_DEFAULT_TEXTURE_WHITE }, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR });
		vulkan_renderer_uniform_set_bind_texture(context->vk_context, dst_material->set, 1, (RhiTexture){ RENDERER_DEFAULT_TEXTURE_WHITE }, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR });
		vulkan_renderer_uniform_set_bind_texture(context->vk_context, dst_material->set, 2, (RhiTexture){ RENDERER_DEFAULT_TEXTURE_NORMAL }, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR });
		vulkan_renderer_uniform_set_bind_texture(context->vk_context, dst_material->set, 3, (RhiTexture){ RENDERER_DEFAULT_TEXTURE_WHITE }, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR });
		vulkan_renderer_uniform_set_bind_texture(context->vk_context, dst_material->set, 4, (RhiTexture){ RENDERER_DEFAULT_TEXTURE_BLACK }, (RhiSampler){ RENDERER_DEFAULT_SAMPLER_LINEAR });
	}

	arena_release_scratch(scratch);

	return result;
}

void player_update(float3 *player_position, float dt, Camera *camera) {
	static float azimuth = C_PIf * 3 / 2.f;
	static float theta = C_PIf / 3.f;

	float yaw_delta = input_mouse_dx() * CAMERA_SENSITIVITY;
	float pitch_delta = input_mouse_dy() * CAMERA_SENSITIVITY;

	azimuth = fmodf(azimuth + yaw_delta, C_PIf * 2.f);
	if (azimuth < 0)
		azimuth += C_PIf * 2.f;

	theta = clamp(theta - pitch_delta, C_PIf / 4.f, C_PIf / 2.f);

	float3 offset = vector3f_subtract(camera->position, *player_position);

	float r = vector3f_length(offset);
	if (r < EPSILON)
		r = EPSILON;

	float current_theta = acosf(offset.y / r);
	float current_azimuth = atan2f(offset.z, offset.x); // [-pi, pi]

	if (current_azimuth < 0)
		current_azimuth += C_PIf * 2.f;

	float da = azimuth - current_azimuth;
	if (da > C_PI)
		da -= C_PI * 2.f;
	if (da < -C_PI)
		da += C_PI * 2.f;

	float lerp = 10.0f * dt;

	current_azimuth += lerp * da;
	current_theta += lerp * (theta - current_theta);

	camera->position = (float3){
		(SPRING_LENGTH * sinf(current_theta) * cosf(current_azimuth)) + player_position->x,
		SPRING_LENGTH * cosf(current_theta),
		(SPRING_LENGTH * sinf(current_theta) * sinf(current_azimuth)) + player_position->z,
	};

	float3 camera_position = camera->position;
	camera_position.y = 0.0f;
	float3 camera_target = camera->target;
	camera_target.y = 0.0f;

	float3 forward, right;

	forward = vector3f_normalize(vector3f_subtract(camera_target, camera_position));

	right = vector3f_cross(forward, camera->up);
	right = vector3f_normalize(right);

	float3 direction = { 0, 0, 0 };

	// Logic: direction += forward * scalar
	float forward_input = (float)(input_key_down(KEY_CODE_W) - input_key_down(KEY_CODE_S));
	direction = vector3f_add(direction, vector3f_scale(forward, forward_input));

	float right_input = (float)(input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A));
	direction = vector3f_add(direction, vector3f_scale(right, right_input));

	if (vector3f_length(direction) > EPSILON)
		direction = vector3f_normalize(direction);

	// Apply movement to player_position (passed by pointer)
	*player_position = vector3f_add(*player_position, vector3f_scale(direction, MOVE_SPEED * dt));

	camera->target.x = player_position->x;
	camera->target.z = player_position->z;
}

static GameInterface interface;
GameInterface *game_hookup(void) {
	interface = (GameInterface){
		.on_update = game_on_update_and_render,
	};
	return &interface;
}
