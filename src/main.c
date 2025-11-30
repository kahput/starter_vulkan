#include "allocators/pool.h"
#include "assets/importer.h"
#include "platform.h"

#include "event.h"

#include "input.h"
#include "input/input_types.h"

#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

#include <cglm/util.h>
#include <cglm/vec3.h>
#include <cgltf/cgltf.h>

#include <fcntl.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include "allocators/arena.h"
#include "common.h"
#include "core/identifiers.h"
#include "core/logger.h"

#include <stdio.h>
#include <string.h>

#define GATE_FILE_PATH "assets/models/modular_dungeon/gate.glb"
#define GATE_DOOR_FILE_PATH "assets/models/modular_dungeon/gate-door.glb"
#define BOT_FILE_PATH "assets/models/characters/gdbot.glb"
#define MAGE_FILE_PATH "assets/models/characters/mage.glb"

Model *load_gltf_model(Arena *arena, const char *path);
bool resize_event(Event *event);
void update_uniforms(VulkanContext *context, Platform *platform, float dt);

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

	Arena *permanent, *frame, *assets;
} state;

int main(void) {
	state.permanent = allocator_arena();
	state.frame = allocator_arena();
	state.assets = allocator_arena();

	uint32_t version = 0;
	vkEnumerateInstanceVersion(&version);
	logger_set_level(LOG_LEVEL_DEBUG);

	event_system_startup();
	input_system_startup();

	LOG_INFO("Vulkan %d.%d.%d", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));

	VulkanContext context = { 0 };
	Platform *platform = platform_startup(state.permanent, 1280, 720, "Starter Vulkan");
	if (platform == NULL) {
		LOG_ERROR("Platform startup failed");
		return -1;
	}

	platform_pointer_mode(platform, PLATFORM_POINTER_DISABLED);

	state.start_time = platform_time_ms(platform);
	event_subscribe(SV_EVENT_WINDOW_RESIZED, resize_event);

	vulkan_renderer_create(state.permanent, platform, &context);

	// ================== DYNAMIC ==================

	// ================== CPU ==================
	//
	// Model *model = load_gltf_model(state.assets, GATE_FILE_PATH);
	// Model *model = load_gltf_model(state.assets, GATE_DOOR_FILE_PATH);
	Model *model = importer_load_gltf(state.assets, BOT_FILE_PATH);
	// Model *model = load_gltf_model(state.assets, MAGE_FILE_PATH);
	Image model_alebdo = importer_load_image(model->primitives->material.base_color_texture.path);

	// ================== GPU ==================

	vulkan_renderer_create_texture(&context, &model_alebdo);
	vulkan_renderer_create_sampler(&context);

	importer_unload_image(model_alebdo);

	ShaderAttribute attributes[] = {
		{ .name = "in_position", .format = SHADER_ATTRIBUTE_FORMAT_FLOAT3, .binding = 0 },
		{ .name = "in_uv", .format = SHADER_ATTRIBUTE_FORMAT_FLOAT2, .binding = 0 },
		{ .name = "in_normal", .format = SHADER_ATTRIBUTE_FORMAT_FLOAT3, .binding = 0 },
	};

	ShaderUniform uniforms[] = {
		{ .name = "u_view_projection", .type = SHADER_UNIFORM_TYPE_BUFFER, .stage = SHADER_STAGE_VERTEX, .array_count = 1, .frequency = SHADER_UNIFORM_FREQUENCY_PER_FRAME },
		{ .name = "u_material_props", .type = SHADER_UNIFORM_TYPE_BUFFER, .stage = SHADER_STAGE_FRAGMENT, .array_count = 1, .frequency = SHADER_UNIFORM_FREQUENCY_PER_MATERIAL },
		{ .name = "u_albedo", .type = SHADER_UNIFORM_TYPE_COMBINED_IMAGE_SAMPLER, .stage = SHADER_STAGE_FRAGMENT, .array_count = 1, .frequency = SHADER_UNIFORM_FREQUENCY_PER_MATERIAL },
		{ .name = "u_normal_map", .type = SHADER_UNIFORM_TYPE_COMBINED_IMAGE_SAMPLER, .stage = SHADER_STAGE_FRAGMENT, .array_count = 1, .frequency = SHADER_UNIFORM_FREQUENCY_PER_MATERIAL },
		{ .name = "u_metallic_roughness", .type = SHADER_UNIFORM_TYPE_COMBINED_IMAGE_SAMPLER, .stage = SHADER_STAGE_FRAGMENT, .array_count = 1, .frequency = SHADER_UNIFORM_FREQUENCY_PER_MATERIAL },
		{ .name = "u_emission", .type = SHADER_UNIFORM_TYPE_COMBINED_IMAGE_SAMPLER, .stage = SHADER_STAGE_FRAGMENT, .array_count = 1, .frequency = SHADER_UNIFORM_FREQUENCY_PER_MATERIAL },
		{ .name = "u_occlusion", .type = SHADER_UNIFORM_TYPE_COMBINED_IMAGE_SAMPLER, .stage = SHADER_STAGE_FRAGMENT, .array_count = 1, .frequency = SHADER_UNIFORM_FREQUENCY_PER_MATERIAL },
		{ .name = "u_model", .type = SHADER_UNIFORM_TYPE_BUFFER, .stage = SHADER_STAGE_FRAGMENT, .array_count = 1, .frequency = SHADER_UNIFORM_FREQUENCY_PER_OBJECT },
	};

	vulkan_create_descriptor_set_layout(&context);
	vulkan_renderer_create_pipeline(&context, "./assets/shaders/vs_default.spv", "./assets/shaders/fs_default.spv", attributes, array_count(attributes));

	for (uint32_t index = 0; index < model->primitive_count; ++index) {
		vulkan_renderer_create_buffer(&context, BUFFER_TYPE_VERTEX, model->primitives[index].vertex_count * sizeof(Vertex), model->primitives[index].vertices);
		vulkan_renderer_create_buffer(&context, BUFFER_TYPE_INDEX, sizeof(uint32_t) * model->primitives->index_count, (void *)model->primitives[index].indices);
	}

	vulkan_create_uniform_buffers(&context);
	vulkan_create_descriptor_pool(&context);
	vulkan_create_descriptor_set(&context);

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

	UUID id = identifier_create();

	Handle handle = handle_create(0);
	handle_increment(&handle);

	while (platform_should_close(platform) == false) {
		float current_frame = (double)(platform_time_ms(platform) - state.start_time) / 1000.0f;
		delta_time = current_frame - last_frame;
		last_frame = current_frame;

		platform_poll_events(platform);
		update_uniforms(&context, platform, delta_time);
		vulkan_renderer_begin_frame(&context, platform);

		for (uint32_t index = 0; index < model->primitive_count; ++index) {
			uint32_t vertex_buffer = (index * 2) + 0;
			uint32_t index_buffer = (index * 2) + 1;

			vulkan_renderer_bind_buffer(&context, vertex_buffer);
			vulkan_renderer_bind_buffer(&context, index_buffer);

			vulkan_renderer_draw_indexed(&context, model->primitives[index].index_count);
		}

		Vulkan_renderer_end_frame(&context);

		if (input_key_down(SV_KEY_LEFTCTRL))
			platform_pointer_mode(platform, PLATFORM_POINTER_NORMAL);
		else
			platform_pointer_mode(platform, PLATFORM_POINTER_DISABLED);

		if (state.resized) {
			LOG_INFO("Recreating Swapchain...");
			logger_indent();
			if (vulkan_recreate_swapchain(&context, platform) == true) {
				LOG_INFO("Swapchain successfully recreated");
			} else {
				LOG_WARN("Failed to recreate swapchain");
			}

			logger_dedent();
			state.resized = false;
		}

		input_system_update();
	}

	input_system_shutdown();
	event_system_shutdown();

	vkDeviceWaitIdle(context.device.logical);

	return 0;
}

void update_uniforms(VulkanContext *context, Platform *platform, float dt) {
	MVPObject mvp = { 0 };
	glm_mat4_identity(mvp.model);

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
	glm_perspective(glm_rad(45.f), (float)context->swapchain.extent.width / (float)context->swapchain.extent.height, 0.1f, 1000.f, mvp.projection);
	mvp.projection[1][1] *= -1;

	memcpy(context->uniform_buffers_mapped[context->current_frame], &mvp, sizeof(MVPObject));
}

bool resize_event(Event *event) {
	state.resized = true;
	return true;
}
