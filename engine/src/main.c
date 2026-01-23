#include "game_interface.h"
#include "input/input_types.h"
#include "platform.h"

#include "scene.h"

#include "renderer.h"
#include "renderer/backend/vulkan_api.h"

#include "event.h"
#include "events/platform_events.h"

#include "input.h"

#include "assets.h"

#include "common.h"
#include "core/arena.h"
#include "core/astring.h"
#include "core/debug.h"
#include "core/logger.h"

#include "platform/filesystem.h"

#include <cglm/cglm.h>
#include <dlfcn.h>
#include <time.h>

#define GATE_FILE_PATH "assets/models/modular_dungeon/gate.glb"
#define GATE_DOOR_FILE_PATH "assets/models/modular_dungeon/gate-door.glb"
#define BOT_FILE_PATH "assets/models/characters/gdbot.glb"
#define MAGE_FILE_PATH "assets/models/characters/mage.glb"

static const float CAMERA_MOVE_SPEED = 5.f;
static const float CAMERA_SENSITIVITY = .001f;
typedef struct {
	String path;
	void *handle;

	uint64_t last_write;
} DynamicLibrary;

typedef struct layer {
	void (*update)(float dt);
} Layer;

static DynamicLibrary game_library = { 0 };
static GameInterface *game = NULL;

bool game_load(GameContext *context);

bool game_on_load(GameContext *context) {
	if (game->on_load)
		return game->on_load(context);
	return false;
}
bool game_on_update(GameContext *context, float dt) {
	if (game->on_update)
		return game->on_update(context, dt);
	return false;
}
bool game_on_unload(GameContext *context) {
	if (game->on_unload)
		return game->on_unload(context);
	return false;
}

void editor_update(float dt);
bool resize_event(Event *event);

static struct State {
	Arena permanent, transient, game;
	Platform display;
	VulkanContext *context;

	Camera editor_camera;
	AssetLibrary library;

	Layer layers[1];
	Layer *current_layer;

	uint64_t start_time;
	uint32_t width, height;
} state;

static bool wireframe = false;
int main(void) {
	state.permanent = arena_create(MiB(512));
	state.transient = arena_create(MiB(4));
	state.game = arena_create(MiB(32));

	logger_set_level(LOG_LEVEL_DEBUG);

	event_system_startup();
	input_system_startup();
	asset_library_startup(&state.library, arena_push(&state.permanent, MiB(128), 1, true), MiB(128));

	platform_startup(&state.permanent, 1280, 720, "Starter Vulkan", &state.display);

	state.width = state.display.physical_width;
	state.height = state.display.physical_height;

	if (vulkan_renderer_create(&state.permanent, &state.display, &state.context) == false) {
		LOG_ERROR("Failed to create vulkan context");
		return 1;
	}

	// Create default textures
	uint8_t WHITE[4] = { 255, 255, 255, 255 };
	vulkan_renderer_texture_create(state.context, RENDERER_DEFAULT_TEXTURE_WHITE, 1, 1,
		TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED, WHITE);

	uint8_t BLACK[4] = { 0, 0, 0, 255 };
	vulkan_renderer_texture_create(state.context, RENDERER_DEFAULT_TEXTURE_BLACK, 1, 1,
		TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED, BLACK);

	uint8_t FLAT_NORMAL[4] = { 128, 128, 255, 255 };
	vulkan_renderer_texture_create(state.context, RENDERER_DEFAULT_TEXTURE_NORMAL, 1, 1,
		TEXTURE_FORMAT_RGBA8, TEXTURE_USAGE_SAMPLED, FLAT_NORMAL);

	// Create default samplers
	vulkan_renderer_sampler_create(state.context, RENDERER_DEFAULT_SAMPLER_LINEAR, LINEAR_SAMPLER);
	vulkan_renderer_sampler_create(state.context, RENDERER_DEFAULT_SAMPLER_NEAREST, NEAREST_SAMPLER);

	// Create render target textures
	vulkan_renderer_texture_create(state.context, RENDERER_DEFAULT_TARGET_SHADOW_DEPTH_MAP,
		state.width, state.height, TEXTURE_FORMAT_DEPTH,
		TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED, NULL);

	vulkan_renderer_texture_create(state.context, RENDERER_DEFAULT_TARGET_MAIN_DEPTH_MAP,
		state.width, state.height, TEXTURE_FORMAT_DEPTH,
		TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED, NULL);

	vulkan_renderer_texture_create(state.context, RENDERER_DEFAULT_TARGET_MAIN_COLOR_MAP,
		state.width, state.height, TEXTURE_FORMAT_RGBA16F,
		TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED, NULL);

	// Create global resources
	vulkan_renderer_resource_global_create(
		state.context, RENDERER_GLOBAL_RESOURCE_SHADOW,
		(ResourceBinding[]){
		  { .binding = 0, .type = SHADER_BINDING_UNIFORM_BUFFER, .size = sizeof(mat4), .count = 1 },
		},
		1);
	vulkan_renderer_resource_global_create(
		state.context, RENDERER_GLOBAL_RESOURCE_MAIN,
		(ResourceBinding[]){
		  { .binding = 0,
			.type = SHADER_BINDING_UNIFORM_BUFFER,
			.size = sizeof(FrameData),
			.count = 1 },
		  { .binding = 1, .type = SHADER_BINDING_TEXTURE_2D, .size = 0, .count = 1 },
		  { .binding = 1, .type = SHADER_BINDING_SAMPLER, .size = 0, .count = 1 },
		},
		3);
	vulkan_renderer_resource_global_set_texture_sampler(state.context, RENDERER_GLOBAL_RESOURCE_MAIN, 1, RENDERER_DEFAULT_TARGET_SHADOW_DEPTH_MAP, RENDERER_DEFAULT_SAMPLER_LINEAR);

	vulkan_renderer_resource_global_create(
		state.context, RENDERER_GLOBAL_RESOURCE_POSTFX,
		(ResourceBinding[]){
		  { .binding = 0, .type = SHADER_BINDING_TEXTURE_2D, .size = 0, .count = 1 },
		  { .binding = 0, .type = SHADER_BINDING_SAMPLER, .size = 0, .count = 1 },
		},
		2);
	vulkan_renderer_resource_global_set_texture_sampler(
		state.context, RENDERER_GLOBAL_RESOURCE_POSTFX, 0, RENDERER_DEFAULT_TARGET_MAIN_COLOR_MAP,
		RENDERER_DEFAULT_SAMPLER_LINEAR);

	// Create render passes
	RenderPassDesc shadow_pass = { .name = S("Shadow"),
		.depth_attachment = {
		  .texture = RENDERER_DEFAULT_TARGET_SHADOW_DEPTH_MAP,
		  .clear = { .depth = 1.0f },
		  .load = CLEAR,
		  .store = STORE,
		},
		.use_depth = true,
		.enable_msaa = false };

	RenderPassDesc main_pass = { .name = S("Main"),
		.color_attachments = { {
		  .texture = RENDERER_DEFAULT_TARGET_MAIN_COLOR_MAP,
		  .clear = { .color = GLM_VEC4_BLACK_INIT },
		  .load = CLEAR,
		  .store = STORE,
		} },
		.color_attachment_count = 1,
		.depth_attachment = {
		  .texture = RENDERER_DEFAULT_TARGET_MAIN_DEPTH_MAP,
		  .clear = { .depth = 1.0f },
		  .load = CLEAR,
		  .store = DONT_CARE,
		},
		.use_depth = true,
		.enable_msaa = true };

	RenderPassDesc postfx_pass = { .name = S("Post"),
		.color_attachments = { { .present = true,
		  .clear = { .color = GLM_VEC4_BLACK_INIT },
		  .load = CLEAR,
		  .store = STORE } },
		.color_attachment_count = 1,
		.enable_msaa = false };

	vulkan_renderer_pass_create(state.context, RENDERER_DEFAULT_PASS_SHADOW, shadow_pass);
	vulkan_renderer_pass_create(state.context, RENDERER_DEFAULT_PASS_MAIN, main_pass);
	vulkan_renderer_pass_create(state.context, RENDERER_DEFAULT_PASS_POSTFX, postfx_pass);

	state.layers[0] = (Layer){ .update = editor_update };
	state.current_layer = &state.layers[0];

	platform_pointer_mode(&state.display, PLATFORM_POINTER_DISABLED);
	state.start_time = platform_time_ms(&state.display);
	event_subscribe(SV_EVENT_WINDOW_RESIZED, resize_event);

	state.editor_camera = (Camera){ .position = { 0.0f, 15.0f, -27.f },
		.target = { 0.0f, 0.0f, 0.0f },
		.up = { 0.0f, 1.0f, 0.0f },
		.fov = 45.f,
		.projection = CAMERA_PROJECTION_PERSPECTIVE };

	float delta_time = 0.0f;
	float last_frame = 0.0f;

	asset_library_track_directory(&state.library, S("assets"));

	GameContext game_context = { .game_memory = state.game.memory, .game_memory_size = state.game.capacity, .vk_context = state.context, &state.library };
	game_load(&game_context);
	game_on_load(&game_context);

	float timer_accumulator = 0.0f;
	uint32_t frames = 0;

	// Create light debug shader
	ShaderSource *light_shader_src = NULL;
	UUID light_shader_id = asset_library_request_shader(&state.library, S("light_debug.glsl"), &light_shader_src);
	uint32_t light_shader_index = 0;
	uint32_t light_material = 0;

	{
		ShaderConfig light_config = {
			.vertex_code = light_shader_src->vertex_shader.content,
			.vertex_code_size = light_shader_src->vertex_shader.size,
			.fragment_code = light_shader_src->fragment_shader.content,
			.fragment_code_size = light_shader_src->fragment_shader.size,
		};

		PipelineDesc desc = DEFAULT_PIPELINE();
		desc.cull_mode = CULL_MODE_NONE;

		ShaderReflection reflection;
		vulkan_renderer_shader_create(&state.permanent, state.context, light_shader_index,
			RENDERER_GLOBAL_RESOURCE_MAIN, &light_config, &reflection);
		vulkan_renderer_shader_variant_create(state.context, light_shader_index,
			RENDERER_DEFAULT_SHADER_VARIANT_STANDARD, RENDERER_DEFAULT_PASS_MAIN, desc);
		desc.polygon_mode = POLYGON_MODE_LINE;
		vulkan_renderer_shader_variant_create(state.context, light_shader_index,
			RENDERER_DEFAULT_SHADER_VARIANT_WIREFRAME, RENDERER_DEFAULT_PASS_MAIN, desc);

		vulkan_renderer_resource_group_create(
			state.context, light_material, light_shader_index, 256);
	}

	ShaderSource *postfx_shader_src = NULL;
	UUID postfx_shader_id = asset_library_request_shader(&state.library, S("postfx.glsl"), &postfx_shader_src);
	uint32_t postfx_shader_index = 1;
	{
		ShaderConfig postfx_config = {
			.vertex_code = postfx_shader_src->vertex_shader.content,
			.vertex_code_size = postfx_shader_src->vertex_shader.size,
			.fragment_code = postfx_shader_src->fragment_shader.content,
			.fragment_code_size = postfx_shader_src->fragment_shader.size,
		};

		PipelineDesc desc = DEFAULT_PIPELINE();
		desc.cull_mode = CULL_MODE_NONE;
		ShaderReflection reflection;
		vulkan_renderer_shader_create(&state.permanent, state.context, postfx_shader_index,
			RENDERER_GLOBAL_RESOURCE_POSTFX, &postfx_config, &reflection);
		vulkan_renderer_shader_variant_create(state.context, postfx_shader_index,
			RENDERER_DEFAULT_SHADER_VARIANT_STANDARD,
			RENDERER_DEFAULT_PASS_POSTFX, desc);
	}

	// clang-format off
	float quadVertices[] = {
		-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
		-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
		 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,

		-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
		 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		 1.0f,  1.0f, 0.0f, 1.0f, 1.0f
	};
	// clang-format on

	uint32_t quadBuffer = 0;
	vulkan_renderer_buffer_create(
		state.context, quadBuffer, BUFFER_TYPE_VERTEX, sizeof(quadVertices), quadVertices);

	float sun_theta = 2 * GLM_PI / 3.f;
	float sun_azimuth = 0;
	Light lights[] = { [0] = { .type = LIGHT_TYPE_DIRECTIONAL,
						 .color = { 0.2f, 0.2f, 1.0f, 0.1f },
						 .as.direction = { 0.0f, 0.0f, 0.0f } },
		[1] = { .type = LIGHT_TYPE_POINT,
		  .color = { 1.0f, 0.5f, 0.2f, 0.8f },
		  .as.position = { 0.0f, 3.0f, 1.0f } } };

	lights[0].as.direction[0] = sin(sun_theta) * cos(sun_azimuth);
	lights[0].as.direction[1] = cos(sun_theta);
	lights[0].as.direction[2] = sin(sun_theta) * sin(sun_azimuth);

	while (platform_should_close(&state.display) == false) {
		float time = platform_time_seconds(&state.display);
		delta_time = time - last_frame;
		last_frame = time;
		delta_time = max(delta_time, 0.0016f);

		uint64_t current_write_time = filesystem_last_modified(S("libgame.so"));
		if (current_write_time != game_library.last_write && current_write_time != 0) {
			LOG_INFO("Game file change detected. Reloading...");
			struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 }; // 100ms
			nanosleep(&ts, NULL);
			game_load(&game_context);
		}

		platform_poll_events(&state.display);

		state.current_layer->update(delta_time);

		lights[1].as.position[0] = cos(time) * 5;
		lights[1].as.position[2] = sin(time) * 5;

		if (vulkan_renderer_frame_begin(state.context, state.display.physical_width,
				state.display.physical_height)) {
			// Main pass
			vulkan_renderer_pass_begin(state.context, RENDERER_DEFAULT_PASS_MAIN);
			{
				FrameData frame_data = { 0 };
				glm_mat4_identity(frame_data.view);
				glm_lookat(state.editor_camera.position, state.editor_camera.target, state.editor_camera.up,
					frame_data.view);

				glm_mat4_identity(frame_data.projection);
				glm_perspective(glm_rad(state.editor_camera.fov), (float)state.width / (float)state.height,
					0.1f, 1000.f, frame_data.projection);
				frame_data.projection[1][1] *= -1;

				uint32_t point_light_count = 0;
				for (uint32_t light_index = 0; light_index < countof(lights); ++light_index) {
					if (lights[light_index].type == LIGHT_TYPE_DIRECTIONAL)
						memcpy(&frame_data.directional_light, lights + light_index, sizeof(Light));
					else
						memcpy(frame_data.lights + point_light_count++, lights + light_index, sizeof(Light));
				}
				glm_vec3_dup(state.editor_camera.position, frame_data.camera_position);
				frame_data.light_count = point_light_count;

				vulkan_renderer_resource_global_write(state.context, RENDERER_GLOBAL_RESOURCE_MAIN, 0,
					sizeof(FrameData), &frame_data);
				vulkan_renderer_resource_global_bind(state.context, RENDERER_GLOBAL_RESOURCE_MAIN);

				game_on_update(&game_context, delta_time);

				// Draw light debug
				vulkan_renderer_shader_bind(state.context, light_shader_index, wireframe);
				for (uint32_t index = 0; index < countof(lights); ++index) {
					if (lights[index].type == LIGHT_TYPE_DIRECTIONAL)
						continue;

					mat4 transform = GLM_MAT4_IDENTITY_INIT;
					glm_translate(transform, lights[index].as.position);

					vulkan_renderer_resource_group_write(state.context, light_material, 0,
						0, sizeof(vec4), lights[index].color, true);
					vulkan_renderer_resource_group_bind(state.context, light_material, 0);
					vulkan_renderer_resource_local_write(state.context, 0, sizeof(mat4), transform);
					vulkan_renderer_buffer_bind(state.context, quadBuffer, 0);
					vulkan_renderer_draw(state.context, 6);
				}
			}
			vulkan_renderer_pass_end(state.context);
			vulkan_renderer_texture_prepare_sample(state.context, RENDERER_DEFAULT_TARGET_MAIN_COLOR_MAP);

			// Postfx pass
			vulkan_renderer_pass_begin(state.context, RENDERER_DEFAULT_PASS_POSTFX);
			{
				vulkan_renderer_shader_bind(
					state.context, postfx_shader_index,
					RENDERER_DEFAULT_SHADER_VARIANT_STANDARD);
				vulkan_renderer_resource_global_bind(state.context, RENDERER_GLOBAL_RESOURCE_POSTFX);
				vulkan_renderer_buffer_bind(state.context, quadBuffer, 0);
				vulkan_renderer_draw(state.context, 6);
			}
			vulkan_renderer_pass_end(state.context);

			Vulkan_renderer_frame_end(state.context);
		}

		if (input_key_pressed(SV_KEY_ENTER))
			wireframe = !wireframe;

		if (input_key_down(SV_KEY_LEFTCTRL))
			platform_pointer_mode(&state.display, PLATFORM_POINTER_NORMAL);
		else
			platform_pointer_mode(&state.display, PLATFORM_POINTER_DISABLED);

		input_system_update();
	}

	vulkan_renderer_destroy(state.context);
	input_system_shutdown();
	event_system_shutdown();

	return 0;
}

void editor_update(float dt) {
	float yaw_delta = -input_mouse_dx() * CAMERA_SENSITIVITY;
	float pitch_delta = -input_mouse_dy() * CAMERA_SENSITIVITY;

	vec3 target_position, camera_right;

	glm_vec3_sub(state.editor_camera.target, state.editor_camera.position, target_position);
	glm_vec3_normalize(target_position);

	glm_vec3_rotate(target_position, yaw_delta, state.editor_camera.up);

	glm_vec3_cross(target_position, state.editor_camera.up, camera_right);
	glm_vec3_normalize(camera_right);

	vec3 camera_down;
	glm_vec3_negate_to(state.editor_camera.up, camera_down);

	float max_angle = glm_vec3_angle(state.editor_camera.up, target_position) - 0.001f;
	float min_angle = -glm_vec3_angle(camera_down, target_position) + 0.001f;

	pitch_delta = clamp(pitch_delta, min_angle, max_angle);

	glm_vec3_rotate(target_position, pitch_delta, camera_right);

	vec3 move = GLM_VEC3_ZERO_INIT;

	glm_vec3_cross(state.editor_camera.up, target_position, camera_right);
	glm_vec3_normalize(camera_right);

	glm_vec3_muladds(camera_right,
		(input_key_down(SV_KEY_D) - input_key_down(SV_KEY_A)) * CAMERA_MOVE_SPEED * dt,
		move);
	glm_vec3_muladds(
		camera_down,
		(input_key_down(SV_KEY_SPACE) - input_key_down(SV_KEY_C)) * CAMERA_MOVE_SPEED * dt, move);
	glm_vec3_muladds(target_position,
		(input_key_down(SV_KEY_S) - input_key_down(SV_KEY_W)) * CAMERA_MOVE_SPEED * dt,
		move);

	glm_vec3_negate(move);
	glm_vec3_add(move, state.editor_camera.position, state.editor_camera.position);
	glm_vec3_add(state.editor_camera.position, target_position, state.editor_camera.target);
}

bool resize_event(Event *event) {
	WindowResizeEvent *wr_event = (WindowResizeEvent *)event;

	if (vulkan_renderer_on_resize(state.context, wr_event->width, wr_event->height)) {
		state.width = wr_event->width;
		state.height = wr_event->height;
	}

	return true;
}

bool game_load(GameContext *context) {
	if (game_library.handle) {
		if (game && game->on_unload)
			game->on_unload(context);

		dlclose(game_library.handle);
		game_library.handle = NULL;
	}
	ArenaTemp scratch = arena_scratch(NULL);

	String path = S("./libgame.so");
	String temp_path = string_format(scratch.arena, "./loaded_%s.so", "game");

	if (filesystem_file_copy(path, temp_path) == false) {
		LOG_ERROR("failed to copy file");
		ASSERT(false);
		return false;
	}

	void *handle = dlopen(temp_path.data, RTLD_NOW);
	if (handle == NULL) {
		LOG_ERROR("dlopen failed: %s", dlerror());
		ASSERT(false);
		return false;
	}

	game_library.handle = handle;
	game_library.path = path;
	game_library.last_write = filesystem_last_modified(path);

	PFN_game_hookup hookup;
	*(void **)(&hookup) = dlsym(game_library.handle, GAME_HOOKUP_NAME);

	if (hookup == false) {
		LOG_ERROR("dlsym failed: %s", dlerror());
		ASSERT(false);
		return false;
	}

	game = hookup();
	return true;
}
