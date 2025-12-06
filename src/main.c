#include "events/platform_events.h"
#include "platform.h"

#include "event.h"
#include "input.h"

#include "assets/importer.h"

#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

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

#define MAX_MESHES 256
#define MAX_MATERIAL_INSTANCES 32

static struct State {
	Arena *permanent, *frame, *assets;
	Camera camera;
	VulkanContext *ctx;

	uint32_t scene_buffer;

	Mesh meshes[MAX_MESHES];
	uint32_t mesh_count;

	Material master_material;

	Texture textures[MAX_TEXTURES];
	uint32_t texture_count;

	MaterialInstance materials[MAX_MATERIAL_INSTANCES];
	uint32_t material_count;

	uint32_t buffer_count, set_count;

	uint64_t start_time;
} state;

int main(void) {
	state.permanent = allocator_arena();
	state.frame = allocator_arena();
	state.assets = allocator_arena();

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

	// ================== DYNAMIC ==================

	// ================== CPU ==================
	//
	// Model *model = load_gltf_model(state.assets, GATE_FILE_PATH);
	// Model *model = load_gltf_model(state.assets, GATE_DOOR_FILE_PATH);

	state.scene_buffer = state.buffer_count++;
	vulkan_renderer_create_buffer(state.ctx, state.scene_buffer, BUFFER_TYPE_UNIFORM, sizeof(mat4) * 2, NULL);

	vulkan_renderer_create_shader(state.ctx, 0, "./assets/shaders/vs_default.spv", "./assets/shaders/fs_default.spv");
	vulkan_renderer_create_pipeline(state.ctx, 0, DEFAULT_PIPELINE(0));

	ArenaTemp temp = arena_begin_temp(state.assets);
	SceneAsset *scene = importer_load_gltf(state.assets, BOT_FILE_PATH);
	upload_scene(scene);
	arena_end_temp(temp);

	vulkan_renderer_create_resource_set(state.ctx, 0, 0, SHADER_UNIFORM_FREQUENCY_PER_FRAME);
	vulkan_renderer_update_resource_set_buffer(state.ctx, SHADER_UNIFORM_FREQUENCY_PER_FRAME, "u_camera", state.scene_buffer);

	state.camera = (Camera){
		.speed = 3.0f,
		.sensitivity = 2.5f,
		.pitch = 0.0f,
		.yaw = 90.f,
		.position = { 0.0f, 1.0f, 5.0f },
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

		vulkan_renderer_update_buffer(state.ctx, state.scene_buffer, 0, sizeof(mat4), &state.camera.view);
		vulkan_renderer_update_buffer(state.ctx, state.scene_buffer, sizeof(mat4), sizeof(mat4), &state.camera.projection);

		vulkan_renderer_begin_frame(state.ctx, platform);
		vulkan_renderer_bind_pipeline(state.ctx, 0);
		vulkan_renderer_bind_resource_set(state.ctx, SHADER_UNIFORM_FREQUENCY_PER_FRAME);

		mat4 model_matrix;
		glm_mat4_identity(model_matrix);
		// glm_mat4_scale(model_matrix, 10);
		vulkan_renderer_push_constants(state.ctx, 0, "push_constants", model_matrix);

		for (uint32_t mesh_index = 0; mesh_index < state.mesh_count; ++mesh_index) {
			Mesh *mesh = &state.meshes[mesh_index];
			vulkan_renderer_bind_buffer(state.ctx, mesh->vertex_buffer);
			vulkan_renderer_bind_buffer(state.ctx, mesh->index_buffer);

			vulkan_renderer_draw_indexed(state.ctx, mesh->index_count);
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

bool upload_scene(SceneAsset *asset) {
	if (state.buffer_count >= MAX_MESHES) {
		LOG_WARN("Main: mesh slots filled, aborting upload");
		return false;
	}

	for (uint32_t mesh_index = 0; mesh_index < asset->mesh_count; ++mesh_index) {
		MeshAsset *asset_mesh = &asset->meshes[mesh_index];
		Mesh *mesh = &state.meshes[state.mesh_count++];

		mesh->vertex_buffer = state.buffer_count++;
		mesh->index_buffer = state.buffer_count++;

		mesh->vertex_count = asset_mesh->vertex_count;
		mesh->index_count = asset_mesh->index_count;

		vulkan_renderer_create_buffer(state.ctx, mesh->vertex_buffer, BUFFER_TYPE_VERTEX, sizeof(Vertex) * asset_mesh->vertex_count, asset_mesh->vertices);
		vulkan_renderer_create_buffer(state.ctx, mesh->index_buffer, BUFFER_TYPE_INDEX, sizeof(uint32_t) * asset_mesh->index_count, asset_mesh->indices);
	}

	for (uint32_t texture_index = 0; texture_index < asset->texture_count; ++texture_index) {
		TextureAsset *asset_texture = &asset->textures[texture_index];
		Texture *texture = &state.textures[state.texture_count];

		texture->texture = state.texture_count++;
		texture->sampler = 0;

		vulkan_renderer_create_texture(state.ctx, texture->texture, asset_texture->width, asset_texture->height, asset_texture->channels, asset_texture->pixels);
	}

	//
	// for (uint32_t material_index = 0; material_index < asset->material_count; ++material_index) {
	// 	MaterialAsset *asset_material = &asset->materials[material_index];
	// 	MaterialInstance *instance = &state.materials[state.material_count++];
	//
	// 	instance->material = 0;
	//
	// 	vulkan_renderer_create_resource_set(state.ctx, instance->resource_set++, state.master_material.shader, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);
	//
	//
	// 	vulkan_renderer_update_resource_set_buffer(state.ctx, SHADER_UNIFORM_FREQUENCY_PER_FRAME, "u_camera", state.uniform_buffer);
	// 	if ()
	// 	vulkan_renderer_update_resource_set_texture_sampler(state.ctx, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL, "texture_sampler", texture, 0);
	// }

	return true;
}

void update_camera(VulkanContext *context, Platform *platform, float dt) {
	CameraUpload mvp = { 0 };

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

	glm_mat4_identity(mvp.projection);
	glm_perspective(glm_rad(45.f), (float)platform->physical_width / (float)platform->physical_height, 0.1f, 1000.f, state.camera.projection);
	state.camera.projection[1][1] *= -1;
}

bool resize_event(Event *event) {
	WindowResizeEvent *wr_event = (WindowResizeEvent *)event;

	vulkan_renderer_resize(state.ctx, wr_event->width, wr_event->height);
	return true;
}
