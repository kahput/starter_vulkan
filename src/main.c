#include "events/platform_events.h"
#include "platform.h"

#include "event.h"
#include "input.h"

#include "assets/importer.h"

#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

#include <assert.h>
#include <cglm/affine-pre.h>
#include <cglm/mat4.h>
#include <cglm/util.h>
#include <cglm/vec3.h>
#include <cgltf/cgltf.h>

#include <fcntl.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include "common.h"
#include "core/identifiers.h"
#include "core/logger.h"

#include "allocators/arena.h"
#include "allocators/pool.h"

#include <stdio.h>
#include <string.h>

#define GATE_FILE_PATH "assets/models/modular_dungeon/gate.glb"
#define GATE_DOOR_FILE_PATH "assets/models/modular_dungeon/gate-door.glb"
#define BOT_FILE_PATH "assets/models/characters/gdbot.glb"
#define MAGE_FILE_PATH "assets/models/characters/mage.glb"

bool resize_event(Event *event);

bool upload_scene(SceneAsset *asset);
void update_camera(VulkanContext *context, Platform *platform, float dt);

static const float camera_speed = 1.0f;

typedef struct camera {
	float speed, sensitivity;

	float yaw, pitch;
	vec3 position, up;

	mat4 view, projection;
} Camera;

#define MAX_MODEL_MESHES 32
#define MAX_MODEL_MATERIALS 8
#define MAX_MODEL_TEXTURES 8
#define MAX_MODELS 8

typedef struct model {
	Mesh meshes[MAX_MODEL_MESHES];
	uint32_t mesh_count;

	MaterialInstance materials[MAX_MODEL_MATERIALS];
	uint32_t material_count;

	Texture textures[MAX_MODEL_TEXTURES];
	uint32_t texture_count;
} Model;

typedef struct Sprite {
	MaterialInstance material;

	Texture texture;
} Sprite;

static struct State {
	Arena *permanent, *frame, *assets;
	Camera camera;
	VulkanContext *ctx;

	uint32_t scene_uniform_buffer;
	Mesh quad_mesh;

	uint32_t default_sampler, sprite_sampler;

	uint32_t default_texture_white;
	uint32_t default_texture_black;
	uint32_t default_texture_normal;
	uint32_t default_texture_offset;

	Model models[MAX_MODELS];
	uint32_t model_count;

	Material model_material, sprite_material;

	uint32_t buffer_count, texture_count, set_count, sampler_count;

	uint64_t start_time;
} state;

int main(void) {
	state.permanent = allocator_arena(MiB(64));
	state.frame = allocator_arena(MiB(4));
	state.assets = allocator_arena(MiB(128));

	logger_set_level(LOG_LEVEL_DEBUG);

	event_system_startup();
	input_system_startup();

	Platform *platform = platform_startup(state.permanent, 1280, 720, "Starter Vulkan");
	if (platform == NULL) {
		LOG_ERROR("Platform startup failed");
		return -1;
	}

	platform_pointer_mode(platform, PLATFORM_POINTER_DISABLED);

	state.start_time = platform_time_ms(platform);
	event_subscribe(SV_EVENT_WINDOW_RESIZED, resize_event);

	vulkan_renderer_create(state.permanent, &state.ctx, platform);

	// ========================= DEFAULT ======================================
	state.scene_uniform_buffer = state.buffer_count++;
	vulkan_renderer_create_buffer(state.ctx, state.scene_uniform_buffer, BUFFER_TYPE_UNIFORM, sizeof(SceneData), NULL);

	state.model_material = (Material){ .shader = 0, .pipeline = 0 };
	vulkan_renderer_create_shader(state.ctx, state.model_material.shader, "./assets/shaders/vs_terrain.spv", "./assets/shaders/fs_terrain.spv");
	vulkan_renderer_create_pipeline(state.ctx, state.model_material.pipeline, DEFAULT_PIPELINE(state.model_material.shader));

	state.sprite_material = (Material){ .shader = 1, .pipeline = 1 };
	vulkan_renderer_create_shader(state.ctx, state.sprite_material.shader, "./assets/shaders/vs_sprite.spv", "./assets/shaders/fs_sprite.spv");
	PipelineDesc sprite_pipeline = DEFAULT_PIPELINE(state.sprite_material.shader);
	sprite_pipeline.cull_mode = CULL_MODE_NONE;
	vulkan_renderer_create_pipeline(state.ctx, state.sprite_material.pipeline, sprite_pipeline);

	state.default_texture_white = state.texture_count++;
	uint8_t WHITE[4] = { 255, 255, 255, 255 };
	vulkan_renderer_create_texture(state.ctx, state.default_texture_white, 1, 1, 4, WHITE);

	state.default_texture_black = state.texture_count++;
	uint8_t BLACK[4] = { 0, 0, 0, 255 };
	vulkan_renderer_create_texture(state.ctx, state.default_texture_black, 1, 1, 4, BLACK);

	state.default_texture_normal = state.texture_count++;
	uint8_t FLAT_NORMAL[4] = { 128, 128, 255, 255 };
	vulkan_renderer_create_texture(state.ctx, state.default_texture_normal, 1, 1, 4, FLAT_NORMAL);

	state.default_texture_offset = state.texture_count;
	state.default_sampler = state.sampler_count++;
	vulkan_renderer_create_sampler(state.ctx, state.default_sampler, DEFAULT_SAMPLER);

	state.sprite_sampler = state.sampler_count++;
	vulkan_renderer_create_sampler(state.ctx, state.sprite_sampler, SPRITE_SAMPLER);

	uint32_t terrain_set = state.set_count++;
	vulkan_renderer_create_resource_set(state.ctx, terrain_set, state.model_material.shader, SHADER_UNIFORM_FREQUENCY_PER_FRAME);
	vulkan_renderer_update_resource_set_buffer(state.ctx, terrain_set, "u_scene", state.scene_uniform_buffer);

	uint32_t sprite_set = state.set_count++;
	vulkan_renderer_create_resource_set(state.ctx, sprite_set, state.sprite_material.shader, SHADER_UNIFORM_FREQUENCY_PER_FRAME);
	vulkan_renderer_update_resource_set_buffer(state.ctx, sprite_set, "u_scene", state.scene_uniform_buffer);

	// ========================= MODELS ======================================
	Vertex quad_vertices[4] = {
		{ .position = { -0.5f, 1.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f }, .uv = { 0.0f, 0.0f }, .tangent = { 1.0f, 0.0f, 0.0f, 1.0f } },
		{ .position = { -0.5f, 0.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f }, .uv = { 0.0f, 1.0f }, .tangent = { 1.0f, 0.0f, 0.0f, 1.0f } },
		{ .position = { 0.5f, 0.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f }, .uv = { 1.0f, 1.0f }, .tangent = { 1.0f, 0.0f, 0.0f, 1.0f } },
		{ .position = { 0.5f, 1.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f }, .uv = { 1.0f, 0.0f }, .tangent = { 1.0f, 0.0f, 0.0f, 1.0f } },
	};
	uint32_t quad_indices[] = { 0, 1, 2, 2, 3, 0 };
	state.quad_mesh = (Mesh){
		.vertex_buffer = state.buffer_count++,
		.index_buffer = state.buffer_count++,
		.vertex_count = countof(quad_vertices),
		.index_count = countof(quad_indices),
		.material = 0,
	};

	vulkan_renderer_create_buffer(state.ctx, state.quad_mesh.vertex_buffer, BUFFER_TYPE_VERTEX, sizeof(quad_vertices), quad_vertices);
	vulkan_renderer_create_buffer(state.ctx, state.quad_mesh.index_buffer, BUFFER_TYPE_INDEX, sizeof(quad_indices), quad_indices);

	ArenaTemp temp = arena_begin_temp(state.assets);
	SceneAsset *scene = NULL;
	scene = importer_load_gltf(state.assets, GATE_DOOR_FILE_PATH);
	upload_scene(scene);
	arena_end_temp(temp);

	temp = arena_begin_temp(state.assets);
	TextureSource *sprite_texture = importer_load_image(temp.arena, "assets/sprites/kenney/tile_0085.png");
	Sprite sprite = {
		.material = { .material = state.sprite_material, .parameter_uniform_buffer = state.scene_uniform_buffer, .resource_set = state.set_count++ },
		.texture = { .texture = state.texture_count++, .sampler = state.sprite_sampler }
	};
	vulkan_renderer_create_texture(state.ctx, sprite.texture.texture, sprite_texture->width, sprite_texture->height, sprite_texture->channels, sprite_texture->pixels);
	arena_end_temp(temp);

	vulkan_renderer_create_resource_set(state.ctx, sprite.material.resource_set, state.sprite_material.shader, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);
	vulkan_renderer_update_resource_set_texture_sampler(state.ctx, sprite.material.resource_set, "u_texture", sprite.texture.texture, sprite.texture.sampler);

	state.camera = (Camera){
		.speed = 3.0f,
		.sensitivity = 2.5f,
		.pitch = 0.0f,
		.yaw = 90.f,
		.position = { 0.0f, 1.0f, 10.0f },
		.up = { 0.0f, 1.0f, 0.0f },
	};

	float delta_time = 0.0f;
	float last_frame = 0.0f;

	while (platform_should_close(platform) == false) {
		float current_frame = (double)(platform_time_ms(platform) - state.start_time) / 1000.0f;
		delta_time = current_frame - last_frame;
		last_frame = current_frame;

		platform_poll_events(platform);

		update_camera(state.ctx, platform, delta_time);

		SceneData data = { 0 };
		glm_mat4_copy(state.camera.view, data.view);
		glm_mat4_copy(state.camera.projection, data.projection);
		glm_vec3_copy(state.camera.position, data.camera_position);
		vulkan_renderer_update_buffer(state.ctx, state.scene_uniform_buffer, 0, sizeof(SceneData), &data);

		vulkan_renderer_begin_frame(state.ctx, platform);

		vulkan_renderer_bind_pipeline(state.ctx, state.sprite_material.pipeline);
		vulkan_renderer_bind_resource_set(state.ctx, sprite_set);

		// Draw Sprite
		{
			mat4 transform;
			glm_mat4_identity(transform);
			glm_translate(transform, (vec3){ 0.f, 0.0f, 5.0f });
			vulkan_renderer_push_constants(state.ctx, state.sprite_material.shader, "push_constants", transform);

			Mesh *mesh = &state.quad_mesh;
			MaterialInstance *material = &sprite.material;

			vulkan_renderer_bind_resource_set(state.ctx, material->resource_set);

			vulkan_renderer_bind_buffer(state.ctx, mesh->vertex_buffer);
			vulkan_renderer_bind_buffer(state.ctx, mesh->index_buffer);

			vulkan_renderer_draw_indexed(state.ctx, mesh->index_count);
		}

		vulkan_renderer_bind_pipeline(state.ctx, state.model_material.pipeline);
		vulkan_renderer_bind_resource_set(state.ctx, terrain_set);

		// Draw Terrain
		for (uint32_t model_index = 0; model_index < state.model_count; ++model_index) {
			Model *model = &state.models[model_index];

			mat4 transform;
			glm_mat4_identity(transform);
			glm_translate(transform, (vec3){ 0.0f, 0.0f, 0.0f });
			vulkan_renderer_push_constants(state.ctx, 0, "push_constants", transform);

			for (uint32_t mesh_index = 0; mesh_index < model->mesh_count; ++mesh_index) {
				Mesh *mesh = &model->meshes[mesh_index];
				MaterialInstance *material = &model->materials[mesh->material];

				vulkan_renderer_bind_resource_set(state.ctx, material->resource_set);

				vulkan_renderer_bind_buffer(state.ctx, mesh->vertex_buffer);
				vulkan_renderer_bind_buffer(state.ctx, mesh->index_buffer);

				vulkan_renderer_draw_indexed(state.ctx, mesh->index_count);
			}
		}

		Vulkan_renderer_end_frame(state.ctx);

		if (input_key_down(SV_KEY_LEFTCTRL))
			platform_pointer_mode(platform, PLATFORM_POINTER_NORMAL);
		else
			platform_pointer_mode(platform, PLATFORM_POINTER_DISABLED);

		input_system_update();
	}

	input_system_shutdown();
	event_system_shutdown();
	vulkan_renderer_destroy(state.ctx);

	return 0;
}

bool upload_scene(SceneAsset *scene) {
	if (state.buffer_count >= MAX_BUFFERS) {
		LOG_WARN("Main: mesh slots filled, aborting upload");
		return false;
	}

	uint32_t texture_offset = state.texture_count;
	Model *dst = &state.models[state.model_count++];

	for (uint32_t texture_index = 0; texture_index < scene->texture_count; ++texture_index) {
		if (texture_index > MAX_MODEL_TEXTURES)
			assert("Model has more than max textures");
		TextureSource *src_texture = &scene->textures[texture_index];
		Texture *dst_texture = &dst->textures[dst->texture_count++];

		dst_texture->texture = state.texture_count++;
		dst_texture->sampler = state.default_sampler;

		vulkan_renderer_create_texture(state.ctx, dst_texture->texture, src_texture->width, src_texture->height, src_texture->channels, src_texture->pixels);
	}

	for (uint32_t material_index = 0; material_index < scene->material_count; ++material_index) {
		if (material_index >= MAX_MODEL_MATERIALS)
			assert("Model has more than max materials");

		MaterialSource *src_material = &scene->materials[material_index];
		MaterialInstance *dst_material = &dst->materials[dst->material_count++];

		dst_material->material = state.model_material;
		dst_material->parameter_uniform_buffer = state.buffer_count++;
		dst_material->resource_set = state.set_count++;

		vulkan_renderer_create_resource_set(state.ctx, dst_material->resource_set, state.model_material.shader, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);

		MaterialParameters parameters = {
			.base_color_factor = {
			  src_material->base_color_factor[0],
			  src_material->base_color_factor[1],
			  src_material->base_color_factor[2],
			  src_material->base_color_factor[3],
			},
			.metallic_factor = src_material->metallic_factor,
			.roughness_factor = src_material->roughness_factor,
			.emissive_factor = {
			  src_material->emissive_factor[0],
			  src_material->emissive_factor[1],
			  src_material->emissive_factor[2],
			}
		};

		vulkan_renderer_create_buffer(state.ctx, dst_material->parameter_uniform_buffer, BUFFER_TYPE_UNIFORM, sizeof(MaterialParameters), &parameters);
		vulkan_renderer_update_resource_set_buffer(state.ctx, dst_material->resource_set, "u_material_parameters", dst_material->parameter_uniform_buffer);

		if (src_material->base_color_texture) {
			uint32_t texture_index = src_material->base_color_texture - scene->textures;
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_base_color_texture", texture_index + texture_offset, state.default_sampler);
		} else {
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_base_color_texture", state.default_texture_white, state.default_sampler);
		}

		if (src_material->metallic_roughness_texture) {
			uint32_t texture_index = src_material->metallic_roughness_texture - scene->textures;
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_metallic_roughness_texture", texture_index + texture_offset, state.default_sampler);
		} else {
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_metallic_roughness_texture", state.default_texture_white, state.default_sampler);
		}

		if (src_material->normal_texture) {
			uint32_t texture_index = src_material->normal_texture - scene->textures;
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_normal_texture", texture_index + texture_offset, state.default_sampler);
		} else {
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_normal_texture", state.default_texture_normal, state.default_sampler);
		}

		if (src_material->occlusion_texture) {
			uint32_t texture_index = src_material->occlusion_texture - scene->textures;
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_occlusion_texture", texture_index + texture_offset, state.default_sampler);
		} else {
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_occlusion_texture", state.default_texture_white, state.default_sampler);
		}

		if (src_material->emissive_texture) {
			uint32_t texture_index = src_material->emissive_texture - scene->textures;
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_emissive_texture", texture_index + texture_offset, state.default_sampler);
		} else {
			vulkan_renderer_update_resource_set_texture_sampler(state.ctx, dst_material->resource_set, "u_emissive_texture", state.default_texture_black, state.default_sampler);
		}
	}

	for (uint32_t mesh_index = 0; mesh_index < scene->mesh_count; ++mesh_index) {
		if (mesh_index > MAX_MODEL_MESHES)
			assert("Model has more than max meshes");

		MeshSource *src_mesh = &scene->meshes[mesh_index];
		Mesh *dst_mesh = &dst->meshes[dst->mesh_count++];

		dst_mesh->vertex_buffer = state.buffer_count++;
		dst_mesh->index_buffer = state.buffer_count++;

		dst_mesh->vertex_count = src_mesh->vertex_count;
		dst_mesh->index_count = src_mesh->index_count;

		dst_mesh->material = src_mesh->material - scene->materials;

		vulkan_renderer_create_buffer(state.ctx, dst_mesh->vertex_buffer, BUFFER_TYPE_VERTEX, sizeof(Vertex) * src_mesh->vertex_count, src_mesh->vertices);
		vulkan_renderer_create_buffer(state.ctx, dst_mesh->index_buffer, BUFFER_TYPE_INDEX, sizeof(uint32_t) * src_mesh->index_count, src_mesh->indices);
	}

	return true;
}

void update_camera(VulkanContext *context, Platform *platform, float dt) {
	static bool use_keys = true;

	if (input_key_pressed(SV_KEY_TAB))
		use_keys = !use_keys;

	state.camera.yaw += input_mouse_delta_x() * state.camera.sensitivity * 0.01f;
	state.camera.pitch += input_mouse_delta_y() * state.camera.sensitivity * 0.01f;

	if (state.camera.pitch > 89.0f)
		state.camera.pitch = 89.0f;
	if (state.camera.pitch < -89.0f)
		state.camera.pitch = -89.0f;

	vec3 camera_forward = {
		cos(glm_rad(state.camera.yaw)) * cos(glm_rad(state.camera.pitch)),
		sin(glm_rad(state.camera.pitch)),
		sin(glm_rad(state.camera.yaw)) * cos(glm_rad(state.camera.pitch)),
	};
	glm_vec3_normalize(camera_forward);

	vec3 camera_right;
	glm_vec3_cross(state.camera.up, camera_forward, camera_right);
	glm_vec3_normalize(camera_right);

	vec3 camera_up;
	glm_vec3_cross(camera_forward, camera_right, camera_up);

	vec3 move;
	glm_vec3_zero(move);

	if (use_keys) {
		glm_vec3_muladds(camera_right, (input_key_down(SV_KEY_D) - input_key_down(SV_KEY_A)) * state.camera.speed * dt, move);
		glm_vec3_muladds(state.camera.up, (input_key_down(SV_KEY_SPACE) - input_key_down(SV_KEY_C)) * state.camera.speed * dt, move);
		glm_vec3_muladds(camera_forward, (input_key_down(SV_KEY_S) - input_key_down(SV_KEY_W)) * state.camera.speed * dt, move);
	}

	// glm_vec3_normalize(move);
	glm_vec3_add(move, state.camera.position, state.camera.position);

	vec3 camera_target;
	glm_vec3_sub(state.camera.position, camera_forward, camera_target);

	glm_mat4_identity(state.camera.view);
	glm_lookat(state.camera.position, camera_target, camera_up, state.camera.view);

	glm_mat4_identity(state.camera.projection);
	glm_perspective(glm_rad(45.f), (float)platform->physical_width / (float)platform->physical_height, 0.1f, 1000.f, state.camera.projection);
	state.camera.projection[1][1] *= -1;
}

bool resize_event(Event *event) {
	WindowResizeEvent *wr_event = (WindowResizeEvent *)event;

	vulkan_renderer_resize(state.ctx, wr_event->width, wr_event->height);
	return true;
}
