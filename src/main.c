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
	Arena *permanent, *frame, *assets;
	Camera camera;
	VulkanContext *ctx;

	uint64_t start_time;
} state;

int main(void) {
	state.permanent = allocator_arena();
	state.frame = allocator_arena();
	state.assets = allocator_arena();

	logger_set_level(LOG_LEVEL_DEBUG);

	event_system_startup();
	input_system_startup();
	asset_system_startup(state.assets);

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
	// GLTFPrimitive *primitive = importer_load_gltf(state.assets, BOT_FILE_PATH);

	// clang-format off
    float positions[] = {
        // positions          // texture coords
         0.5f,  0.5f, 0.0f,    // top right
         0.5f, -0.5f, 0.0f,    // bottom right
        -0.5f, -0.5f, 0.0f,    // bottom left
        -0.5f,  0.5f, 0.0f,    // top left 
    };

	float uvs[] = {
		1.0f, 1.0f,
		1.0f, 0.0f,
		0.0f, 0.0f,
		0.0f, 1.0f 
	};

    uint32_t indices[] = {
        0, 1, 3, // first triangle
        1, 2, 3  // second triangle
    };
	// clang-format on

	GLTFPrimitive primitive = {
		.positions = positions,
		.uvs = uvs,
		.normals = NULL,
		.indices = indices,
		.index_count = 6,
		.vertex_count = 4
	};

	// ================== GPU ==================

	// vulkan_renderer_create_texture(state.ctx, 0, &model_alebdo);
	// vulkan_renderer_create_sampler(state.ctx, 0);

	vulkan_renderer_create_shader(state.ctx, 0, "./assets/shaders/vs_default.spv", "./assets/shaders/fs_default.spv");

	ShaderAttribute quad_attributes[] = {
		{ .name = "u_position", .format = SHADER_ATTRIBUTE_FORMAT_FLOAT3, .binding = 0 },
		{ .name = "u_uv", .format = SHADER_ATTRIBUTE_FORMAT_FLOAT2, .binding = 1 },
	};

	PipelineDesc default_pipeline = DEFAULT_PIPELINE(0);
	default_pipeline.cull_mode = CULL_MODE_NONE;
	default_pipeline.attributes = quad_attributes;
	default_pipeline.attribute_count = array_count(quad_attributes);

	vulkan_renderer_create_pipeline(state.ctx, 0, default_pipeline);

	vulkan_renderer_create_buffer(state.ctx, 0, BUFFER_TYPE_UNIFORM, sizeof(mat4) * 2, NULL);
	vulkan_renderer_create_resource_set(state.ctx, 0, 0, SHADER_UNIFORM_FREQUENCY_PER_FRAME);

	vulkan_renderer_update_resource_set_buffer(state.ctx, SHADER_UNIFORM_FREQUENCY_PER_FRAME, "u_camera", 0);
	// vulkan_renderer_update_resource_set_texture_sampler(state.ctx, 0, "texture_sampler", 0, 0);

	vulkan_renderer_create_buffer(state.ctx, 1, BUFFER_TYPE_VERTEX, primitive.vertex_count * sizeof(float) * 3, primitive.positions);
	vulkan_renderer_create_buffer(state.ctx, 2, BUFFER_TYPE_VERTEX, primitive.vertex_count * sizeof(float) * 2, primitive.uvs);
	vulkan_renderer_create_buffer(state.ctx, 3, BUFFER_TYPE_INDEX, primitive.index_count * sizeof(uint32_t), primitive.indices);

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
		// glm_mat4_scale(model_matrix, 10);
		vulkan_renderer_push_constants(state.ctx, 0, "push_constants", model_matrix);

		uint32_t vbs[] = { 1, 2 };

		vulkan_renderer_bind_buffers(state.ctx, vbs, array_count(vbs));
		vulkan_renderer_bind_buffer(state.ctx, 3);

		vulkan_renderer_draw_indexed(state.ctx, primitive.index_count);

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
	glm_perspective(glm_rad(45.f), (float)platform->physical_width / (float)platform->physical_height, 0.1f, 1000.f, mvp.projection);
	mvp.projection[1][1] *= -1;

	vulkan_renderer_update_buffer(context, 0, 0, sizeof(CameraUpload), &mvp);
}

bool resize_event(Event *event) {
	WindowResizeEvent *wr_event = (WindowResizeEvent *)event;

	vulkan_renderer_resize(state.ctx, wr_event->width, wr_event->height);
	return true;
}
