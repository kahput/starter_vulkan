#include "platform.h"

#include "event.h"

#include "input.h"
#include "input/input_types.h"

#include "assets/importer.h"

#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

#include <cglm/affine-pre.h>
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

Model *load_gltf_model(Arena *arena, const char *path);
bool resize_event(Event *event);
void update_camera(VulkanContext *context, Platform *platform, float dt);

static const float camera_speed = 1.0f;

typedef struct camera {
	float speed, sensitivity;

	float yaw, pitch;
	vec3 position, up;
} Camera;

static struct State {
	bool resized;
	uint64_t start_time;

	Camera camera;

	VulkanContext *ctx;

	Arena *permanent, *frame, *assets;
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
	Model *model = importer_load_gltf(state.assets, BOT_FILE_PATH);
	// Model *model = load_gltf_model(state.assets, MAGE_FILE_PATH);
	Image model_alebdo = importer_load_image(model->primitives->material.base_color_texture.path);

	// ================== GPU ==================

	vulkan_renderer_create_texture(state.ctx, 0, &model_alebdo);
	vulkan_renderer_create_sampler(state.ctx, 0);

	importer_unload_image(model_alebdo);

	vulkan_renderer_create_shader(state.ctx, 0, "./assets/shaders/vs_default.spv", "./assets/shaders/fs_default.spv");
	vulkan_renderer_create_pipeline(state.ctx, 0, 0);

	vulkan_renderer_create_buffer(state.ctx, 0, BUFFER_TYPE_UNIFORM, sizeof(mat4) * 2, NULL);
	vulkan_renderer_create_resource_set(state.ctx, 0, 0, 0);

	vulkan_renderer_update_resource_set_buffer(state.ctx, 0, "u_camera", 0);
	vulkan_renderer_update_resource_set_texture_sampler(state.ctx, 0, "texture_sampler", 0, 0);

	for (uint32_t index = 0, store_index = 1; index < model->primitive_count; ++index) {
		vulkan_renderer_create_buffer(state.ctx, store_index++, BUFFER_TYPE_VERTEX, model->primitives[index].vertex_count * sizeof(Vertex), model->primitives[index].vertices);
		vulkan_renderer_create_buffer(state.ctx, store_index++, BUFFER_TYPE_INDEX, sizeof(uint32_t) * model->primitives->index_count, (void *)model->primitives[index].indices);
	}

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
		vulkan_renderer_begin_frame(state.ctx, platform);
		vulkan_renderer_bind_pipeline(state.ctx, 0);
		vulkan_renderer_bind_resource_set(state.ctx, 0);

		mat4 model_matrix;
		glm_mat4_identity(model_matrix);
		vulkan_renderer_push_constants(state.ctx, 0, "push_constants", model_matrix);

		for (uint32_t index = 0, retrive_index = 1; index < model->primitive_count; ++index) {
			vulkan_renderer_bind_buffer(state.ctx, retrive_index++);
			vulkan_renderer_bind_buffer(state.ctx, retrive_index++);

			vulkan_renderer_draw_indexed(state.ctx, model->primitives[index].index_count);
		}

		vec4 offset = { 5.0f, 0.0f, 0.0f };
		glm_translate(model_matrix, offset);
		vulkan_renderer_push_constants(state.ctx, 0, "push_constants", model_matrix);

		for (uint32_t index = 0, retrive_index = 1; index < model->primitive_count; ++index) {
			vulkan_renderer_bind_buffer(state.ctx, retrive_index++);
			vulkan_renderer_bind_buffer(state.ctx, retrive_index++);

			vulkan_renderer_draw_indexed(state.ctx, model->primitives[index].index_count);
		}

		Vulkan_renderer_end_frame(state.ctx);

		if (input_key_down(SV_KEY_LEFTCTRL))
			platform_pointer_mode(platform, PLATFORM_POINTER_NORMAL);
		else
			platform_pointer_mode(platform, PLATFORM_POINTER_DISABLED);

		// if (state.resized) {
		// 	LOG_INFO("Recreating Swapchain...");
		// 	logger_indent();
		// 	if (vulkan_recreate_swapchain(&context, platform) == true) {
		// 		LOG_INFO("Swapchain successfully recreated");
		// 	} else {
		// 		LOG_WARN("Failed to recreate swapchain");
		// 	}
		//
		// 	logger_dedent();
		// 	state.resized = false;
		// }

		input_system_update();
	}

	input_system_shutdown();
	event_system_shutdown();
	vulkan_renderer_destroy(state.ctx);

	return 0;
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

	glm_mat4_identity(mvp.view);
	glm_lookat(state.camera.position, camera_target, camera_up, mvp.view);

	glm_mat4_identity(mvp.projection);
	glm_perspective(glm_rad(45.f), (float)platform->physical_height / (float)platform->physical_height, 0.1f, 1000.f, mvp.projection);
	mvp.projection[1][1] *= -1;

	vulkan_renderer_update_buffer(context, 0, 0, sizeof(CameraUpload), &mvp);
}

bool resize_event(Event *event) {
	state.resized = true;
	return true;
}
