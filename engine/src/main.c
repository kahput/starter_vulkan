#include "game_interface.h"
#include "input/input_types.h"
#include "platform.h"
#include "scene.h"
#include "renderer.h"
#include "renderer/r_internal.h"
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

#include <math.h>
#include "core/cmath.h"

#include <dlfcn.h>
#include <string.h>
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
FrameInfo game_on_update_and_render(GameContext *context, float dt) {
	if (game->on_update)
		return game->on_update(context, dt);
	return (FrameInfo){ 0 };
}

void editor_update(float dt);
bool resize_event(EventCode code, void *event, void *receiver);
RhiTexture load_cubemap(String path);

static struct State {
	Arena permanent, transient, game;
	Platform display;
	VulkanContext *context;
	Camera editor_camera;
	AssetLibrary asset_system;
	InputState *input_system;
	EventState *event_system;
	Layer layers[1];
	Layer *current_layer;
	bool editor;
	uint64_t start_time;
	uint32_t width, height;
	RhiTexture white_tex, black_tex, flat_normal_tex;
	RhiSampler linear_sampler, nearest_sampler;
	RhiTexture shadow_depth_tex;
	RhiTexture main_depth_tex;
	RhiTexture main_color_tex;
	RhiTexture skybox_tex;

	RhiUniformSet global_shadow;

	RhiUniformSet global_main;
	RhiBuffer global_main_buffer;

	RhiUniformSet global_postfx;

	RhiBuffer quad_vb;
	RhiShader light_shader;

	RhiUniformSet light_material;
	RhiBuffer light_buffer;

	RhiShader postfx_shader;
	RhiShader skybox_shader;
	RhiUniformSet skybox_material;
} state;

int main(void) {
	state.permanent = arena_create(MiB(512));
	state.transient = arena_create(MiB(4));
	logger_set_level(LOG_LEVEL_DEBUG);

	state.event_system = event_system_startup(&state.permanent);
	state.input_system = input_system_startup(&state.permanent);
	asset_library_startup(&state.asset_system, arena_push(&state.permanent, MiB(128), 1, true), MiB(128));

	platform_startup(&state.permanent, 1280, 720, "Starter Vulkan", &state.display);
	state.width = state.display.physical_width;
	state.height = state.display.physical_height;

	if (vulkan_renderer_create(&state.permanent, &state.display, &state.context) == false) {
		LOG_ERROR("Failed to create vulkan context");
		return 1;
	}

	// Create default textures
	uint8_t WHITE[4] = { 255, 255, 255, 255 };
	state.white_tex = vulkan_renderer_texture_create(
		state.context, 1, 1,
		TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED, WHITE);

	uint8_t BLACK[4] = { 0, 0, 0, 255 };
	state.black_tex = vulkan_renderer_texture_create(
		state.context, 1, 1,
		TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED, BLACK);

	uint8_t FLAT_NORMAL[4] = { 128, 128, 255, 255 };
	state.flat_normal_tex = vulkan_renderer_texture_create(
		state.context, 1, 1,
		TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8, TEXTURE_USAGE_SAMPLED, FLAT_NORMAL);

	// Create default samplers
	state.linear_sampler = vulkan_renderer_sampler_create(
		state.context, LINEAR_SAMPLER);
	state.nearest_sampler = vulkan_renderer_sampler_create(
		state.context, NEAREST_SAMPLER);

	// Create render target textures
	state.shadow_depth_tex = vulkan_renderer_texture_create(
		state.context,
		state.width, state.height,
		TEXTURE_TYPE_2D, TEXTURE_FORMAT_DEPTH, TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED, NULL);
	state.main_depth_tex = vulkan_renderer_texture_create(
		state.context,
		state.width, state.height,
		TEXTURE_TYPE_2D, TEXTURE_FORMAT_DEPTH,
		TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED,
		NULL);
	state.main_color_tex = vulkan_renderer_texture_create(state.context,
		state.width, state.height,
		TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA16F,
		TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_SAMPLED, NULL);

	// Create global resources
	state.global_shadow = vulkan_renderer_uniform_set_create_ex(
		state.context,
		(ResourceBinding[]){
		  { .binding = 0, .type = SHADER_BINDING_UNIFORM_BUFFER, .size = sizeof(Matrix4f), .count = 1 },
		},
		1);

	state.global_main_buffer = vulkan_renderer_buffer_create(
		state.context, BUFFER_TYPE_UNIFORM, sizeof(FrameData), NULL);

	state.global_main = vulkan_renderer_uniform_set_create_ex(
		state.context,
		(ResourceBinding[]){
		  { .binding = 0, .type = SHADER_BINDING_UNIFORM_BUFFER, .size = sizeof(FrameData), .count = 1 },
		  { .binding = 1, .type = SHADER_BINDING_TEXTURE_2D, .size = 0, .count = 1 },
		},
		2);
	vulkan_renderer_uniform_set_bind_buffer(
		state.context, state.global_main, 0, state.global_main_buffer);
	vulkan_renderer_uniform_set_bind_texture(
		state.context, state.global_main, 1, state.shadow_depth_tex, state.linear_sampler);

	state.global_postfx = vulkan_renderer_uniform_set_create_ex(
		state.context,
		(ResourceBinding[]){
		  { .binding = 0, .type = SHADER_BINDING_TEXTURE_2D, .size = 0, .count = 1 },
		},
		1);
	vulkan_renderer_uniform_set_bind_texture(
		state.context, state.global_postfx, 0,
		state.main_color_tex, state.linear_sampler);

	// Create render passes
	DrawListDesc shadow_pass = {
		.name = str_lit("Shadow"),
		.depth_attachment = {
		  .texture = state.shadow_depth_tex,
		  .clear = { .depth = 1.0f },
		  .load = CLEAR,
		  .store = STORE,
		},
		.use_depth = true,
		.msaa_level = 1
	};

	DrawListDesc main_pass = {
		.name = str_lit("Main"),
		.color_attachments = { {
		  .texture = state.main_color_tex,
		  .clear = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
		  .load = CLEAR,
		  .store = STORE,
		} },
		.color_attachment_count = 1,
		.depth_attachment = {
		  .texture = state.main_depth_tex,
		  .clear = { .depth = 1.0f },
		  .load = CLEAR,
		  .store = DONT_CARE,
		},
		.use_depth = true,
		.msaa_level = 4
	};

	DrawListDesc postfx_pass = {
		.name = str_lit("Post"),
		.color_attachments = {
		  [0] = {
			.clear = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
			.load = CLEAR,
			.store = STORE,
		  },
		},
		.color_attachment_count = 1,
		.msaa_level = 1
	};

	state.layers[0] = (Layer){ .update = editor_update };
	state.current_layer = &state.layers[0];

	platform_pointer_mode(&state.display, PLATFORM_POINTER_DISABLED);
	state.start_time = platform_time_ms(&state.display);

	event_subscribe(EVENT_PLATFORM_WINDOW_RESIZED, resize_event, NULL);

	state.editor_camera = (Camera){ .position = { 0.0f, 15.0f, -27.f },
		.target = { 0.0f, 0.0f, 0.0f },
		.up = { 0.0f, 1.0f, 0.0f },
		.fov = 45.f,
		.projection = CAMERA_PROJECTION_PERSPECTIVE };

	float delta_time = 0.0f;
	float last_frame = 0.0f;

	asset_library_track_directory(&state.asset_system, str_lit("assets"));
	state.skybox_tex = load_cubemap(str_lit("assets/textures/skybox/"));

	GameContext game_context = {
		.permanent_memory = arena_push(&state.permanent, MiB(32), 1, true),
		.permanent_memory_size = MiB(32),
		.vk_context = state.context,
		&state.asset_system,
	};

	game_load(&game_context);

	float timer_accumulator = 0.0f;
	uint32_t frames = 0;

	// Create light debug shader
	ShaderSource *light_shader_src = NULL;
	UUID light_shader_id = asset_library_request_shader(&state.asset_system, str_lit("light_debug.glsl"), &light_shader_src);
	{
		ShaderConfig light_config = {
			.vertex_code = light_shader_src->vertex_shader.content,
			.vertex_code_size = light_shader_src->vertex_shader.size,
			.fragment_code = light_shader_src->fragment_shader.content,
			.fragment_code_size = light_shader_src->fragment_shader.size,
		};
		// PipelineDesc desc = DEFAULT_PIPELINE();
		// desc.cull_mode = CULL_MODE_NONE;
		ShaderReflection reflection;
		state.light_shader = vulkan_renderer_shader_create(
			&state.permanent, state.context, &light_config, &reflection);
		state.light_buffer = vulkan_renderer_buffer_create(state.context, BUFFER_TYPE_UNIFORM, sizeof(float4), NULL);
		state.light_material = vulkan_renderer_uniform_set_create(
			state.context, state.light_shader, 1);
		vulkan_renderer_uniform_set_bind_buffer(
			state.context, state.light_material, 0, state.light_buffer);
	}

	ShaderSource *postfx_shader_src = NULL;
	UUID postfx_shader_id = asset_library_request_shader(&state.asset_system, str_lit("postfx.glsl"), &postfx_shader_src);
	{
		ShaderConfig postfx_config = {
			.vertex_code = postfx_shader_src->vertex_shader.content,
			.vertex_code_size = postfx_shader_src->vertex_shader.size,
			.fragment_code = postfx_shader_src->fragment_shader.content,
			.fragment_code_size = postfx_shader_src->fragment_shader.size,
		};
		// PipelineDesc desc = DEFAULT_PIPELINE();
		// desc.cull_mode = CULL_MODE_NONE;
		ShaderReflection reflection;
		state.postfx_shader = vulkan_renderer_shader_create(
			&state.permanent, state.context, &postfx_config, &reflection);
	}

	ShaderSource *skybox_shader_src = NULL;
	UUID skybox_shader_id = asset_library_request_shader(&state.asset_system, str_lit("skybox.glsl"), &skybox_shader_src);
	{
		ShaderConfig skybox_config = {
			.vertex_code = skybox_shader_src->vertex_shader.content,
			.vertex_code_size = skybox_shader_src->vertex_shader.size,
			.fragment_code = skybox_shader_src->fragment_shader.content,
			.fragment_code_size = skybox_shader_src->fragment_shader.size,
		};
		// PipelineDesc desc = DEFAULT_PIPELINE();
		// desc.cull_mode = CULL_MODE_NONE;
		// desc.depth_compare_op = COMPARE_OP_LESS_OR_EQUAL;
		ShaderReflection reflection;
		state.skybox_shader = vulkan_renderer_shader_create(
			&state.permanent, state.context, &skybox_config, &reflection);
		state.skybox_material = vulkan_renderer_uniform_set_create(state.context, state.skybox_shader, 1);
		vulkan_renderer_uniform_set_bind_texture(state.context, state.skybox_material, 0, state.skybox_tex, state.linear_sampler);
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
	state.quad_vb = vulkan_renderer_buffer_create(
		state.context, BUFFER_TYPE_VERTEX, sizeof(quadVertices), quadVertices);

	// --- CHANGED: Updated math constants and array access ---
	float sun_theta = 2 * C_PI / 3.f;
	float sun_azimuth = 0;

	// Assuming Light struct is updated to use float3, or we cast.
	// Accessing via .x .y .z instead of array indices.
	Light lights[] = {
		[0] = { .type = LIGHT_TYPE_DIRECTIONAL,
		  .color = { 1.0f, 1.0f, 1.0f, 1.0f },
		  .as.direction = { 0.0f, 0.0f, 0.0f, 0.0f } },
		[1] = { .type = LIGHT_TYPE_POINT,
		  .color = { 1.0f, 0.5f, 0.2f, 0.8f },
		  .as.position = { 0.0f, 3.0f, 1.0f, 0.0f } }
	};

	lights[0].as.direction.x = sinf(sun_theta) * cosf(sun_azimuth);
	lights[0].as.direction.y = cosf(sun_theta);
	lights[0].as.direction.z = sinf(sun_theta) * sinf(sun_azimuth);

	PipelineDesc pipeline = DEFAULT_PIPELINE;

	while (platform_should_close(&state.display) == false) {
		float time = platform_time_seconds(&state.display);
		delta_time = time - last_frame;
		last_frame = time;
		delta_time = max(delta_time, 0.0016f);

		uint64_t current_write_time = filesystem_last_modified(str_lit("libgame.so"));
		if (current_write_time != game_library.last_write && current_write_time != 0) {
			LOG_INFO("Game file change detected. Reloading...");
			struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
			nanosleep(&ts, NULL);
			game_load(&game_context);
		}

		platform_poll_events(&state.display);

		lights[1].as.position.x = cosf(time) * 5;
		lights[1].as.position.z = sinf(time) * 5;

		if (vulkan_renderer_frame_begin(state.context, state.display.physical_width,
				state.display.physical_height)) {
			// Main pass
			vulkan_renderer_draw_list_begin(state.context, main_pass);
			{
				FrameData frame_data = { 0 };

				// --- CHANGED: Matrix math using cmath functions ---
				frame_data.view = matrix4f_identity();
				frame_data.projection = matrix4f_identity();

				frame_data.projection = matrix4f_perspective(
					DEG2RAD(state.editor_camera.fov),
					(float)state.width / (float)state.height,
					0.1f,
					1000.f);

				// Flip Y for Vulkan (element 5 is y,y in 4x4)
				frame_data.projection.elements[5] *= -1;

				uint32_t point_light_count = 0;
				for (uint32_t light_index = 0; light_index < countof(lights); ++light_index) {
					if (lights[light_index].type == LIGHT_TYPE_DIRECTIONAL)
						memcpy(&frame_data.directional_light, lights + light_index, sizeof(Light));
					else
						memcpy(frame_data.lights + point_light_count++, lights + light_index, sizeof(Light));
				}
				frame_data.light_count = point_light_count;

				vulkan_renderer_uniform_set_bind(state.context, state.global_main);

				FrameInfo game_frame = game_on_update_and_render(&game_context, delta_time);

				if (state.editor) {
					state.current_layer->update(delta_time);
					frame_data.view = matrix4f_lookat(
						state.editor_camera.position,
						state.editor_camera.target,
						state.editor_camera.up);
					frame_data.camera_position = state.editor_camera.position;
				} else {
					frame_data.view = matrix4f_lookat(
						game_frame.camera.position,
						game_frame.camera.target,
						game_frame.camera.up);
					frame_data.camera_position = game_frame.camera.position;
				}

				vulkan_renderer_buffer_write(state.context, state.global_main_buffer, 0,
					sizeof(FrameData), &frame_data);

				// Draw light debug
				vulkan_renderer_shader_bind(state.context, state.light_shader, pipeline);
				for (uint32_t index = 0; index < countof(lights); ++index) {
					if (lights[index].type == LIGHT_TYPE_DIRECTIONAL)
						continue;

					// --- CHANGED: Transform logic ---
					Matrix4f transform = matrix4f_translated(*(float3 *)vector4f_elements(&lights[index].as.position));

					vulkan_renderer_buffer_write(state.context, state.light_buffer,
						0, sizeof(float4), &lights[index].color); // Changed vector4 to float4
					vulkan_renderer_uniform_set_bind(state.context, state.light_material);
					vulkan_renderer_push_constants(state.context, 0, sizeof(Matrix4f), &transform); // Changed mat4 to Matrix4f
					vulkan_renderer_buffer_bind(state.context, state.quad_vb, 0);
					vulkan_renderer_draw(state.context, 6);
				}

				// Draw skybox
				PipelineDesc skybox_pipeline = DEFAULT_PIPELINE;
				skybox_pipeline.depth_compare_op = COMPARE_OP_LESS_OR_EQUAL;
				vulkan_renderer_shader_bind(state.context, state.skybox_shader, skybox_pipeline);
				vulkan_renderer_uniform_set_bind(state.context, state.skybox_material);
				vulkan_renderer_draw(state.context, 36);
			}
			vulkan_renderer_draw_list_end(state.context);

			vulkan_renderer_texture_prepare_sample(state.context, state.main_color_tex);

			// Postfx pass
			vulkan_renderer_draw_list_begin(state.context, postfx_pass);
			{
				vulkan_renderer_shader_bind(
					state.context, state.postfx_shader,
					DEFAULT_PIPELINE);
				vulkan_renderer_uniform_set_bind(state.context, state.global_postfx);
				vulkan_renderer_buffer_bind(state.context, state.quad_vb, 0);
				vulkan_renderer_draw(state.context, 6);
			}
			vulkan_renderer_draw_list_end(state.context);
			Vulkan_renderer_frame_end(state.context);
		}

		if (input_key_pressed(KEY_CODE_ENTER))
			pipeline.polygon_mode = !pipeline.polygon_mode;
		if (input_key_pressed(KEY_CODE_TAB))
			state.editor = !state.editor;
		if (input_key_down(KEY_CODE_LEFTCTRL))
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

	float3 target_position = vector3f_normalize(
		vector3f_subtract(state.editor_camera.target, state.editor_camera.position));

	// Yaw rotation (around up)
	target_position = vector3f_rotate(target_position, yaw_delta, state.editor_camera.up);

	// Calc right
	float3 camera_right = vector3f_cross(target_position, state.editor_camera.up);
	camera_right = vector3f_normalize(camera_right);

	// Calc down
	float3 camera_down = vector3f_negate(state.editor_camera.up);

	// Pitch clamping
	float max_angle = vector3f_angle(state.editor_camera.up, target_position) - 0.001f;
	float min_angle = -vector3f_angle(camera_down, target_position) + 0.001f;

	// Helper to clamp float
	if (pitch_delta > max_angle)
		pitch_delta = max_angle;
	if (pitch_delta < min_angle)
		pitch_delta = min_angle;

	// Pitch rotation (around right)
	target_position = vector3f_rotate(target_position, pitch_delta, camera_right);

	float3 move = { 0, 0, 0 };

	// Re-calculate right after rotation for movement
	camera_right = vector3f_cross(state.editor_camera.up, target_position);
	camera_right = vector3f_normalize(camera_right);

	float right_amount = (input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A)) * CAMERA_MOVE_SPEED * dt;
	float up_amount = (input_key_down(KEY_CODE_SPACE) - input_key_down(KEY_CODE_C)) * CAMERA_MOVE_SPEED * dt;
	float forward_amount = (input_key_down(KEY_CODE_S) - input_key_down(KEY_CODE_W)) * CAMERA_MOVE_SPEED * dt;

	move = vector3f_add(move, vector3f_scale(camera_right, right_amount));
	move = vector3f_add(move, vector3f_scale(camera_down, up_amount));
	move = vector3f_add(move, vector3f_scale(target_position, forward_amount));

	move = vector3f_negate(move);

	// Apply move
	state.editor_camera.position = vector3f_add(move, state.editor_camera.position);
	state.editor_camera.target = vector3f_add(state.editor_camera.position, target_position);
}

bool resize_event(EventCode code, void *event, void *receiver) {
	WindowResizeEvent *wr_event = (WindowResizeEvent *)event;
	if (vulkan_renderer_on_resize(state.context, wr_event->width, wr_event->height)) {
		state.width = wr_event->width;
		state.height = wr_event->height;
		vulkan_renderer_texture_resize(state.context, state.main_depth_tex, state.width, state.height);
		vulkan_renderer_texture_resize(state.context, state.main_color_tex, state.width, state.height);
		vulkan_renderer_uniform_set_bind_texture(state.context, state.global_postfx, 0, state.main_color_tex, state.linear_sampler);
	}
	return true;
}

bool game_load(GameContext *context) {
	if (game_library.handle) {
		dlclose(game_library.handle);
		game_library.handle = NULL;
	}

	ArenaTemp scratch = arena_scratch(NULL);
	String path = str_lit("./libgame.so");
	String temp_path = string_pushf(scratch.arena, "./loaded_%s.so", "game");

	if (filesystem_file_copy(path, temp_path) == false) {
		LOG_ERROR("failed to copy file");
		ASSERT(false);
		return false;
	}

	void *handle = dlopen(temp_path.memory, RTLD_NOW);
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

RhiTexture load_cubemap(String path) {
	ArenaTemp scratch = arena_scratch(NULL);
	StringList files = filesystem_list_files(scratch.arena, path, false);
	LOG_INFO("Scratch size used so after file_list: %llu", scratch.arena->offset);

	if (files.node_count != 6) {
		LOG_ERROR("Cubemap error: Found %llu files in '%.*s', expected 6.", files.node_count, str_expand(path));
		arena_release_scratch(scratch);
		return (RhiTexture){ 0 }; // Return invalid handle
	}

	ImageSource loaded_images[6];
	int32_t width = 0, height = 0;
	int32_t channels = 0;
	size_t single_image_size = 0;

	static const char *order[6] = {
		"right.",
		"left.",
		"top.",
		"bottom.",
		"front.",
		"back.",
	};

	StringNode *file_node = files.first;
	String extension = string_path_extension(file_node->string);

	for (int index = 0; index < 6; index++) {
		ImageSource img = { 0 };
		String key = string_push_concat(scratch.arena, string_from_cstr(order[index]), extension);
		asset_library_load_image(scratch.arena, &state.asset_system, key, &img);
		LOG_INFO("Scratch size used so after image %d: %llu", index, scratch.arena->offset);
		loaded_images[index] = img;

		if (index == 0) {
			width = img.width;
			height = img.height;
			channels = img.channels;
			single_image_size = width * height * channels;
		} else
			ASSERT(img.width == width && img.height == height);

		file_node = file_node->next;
	}

	uint8_t *final_pixels = arena_push_array(scratch.arena, uint8_t, single_image_size * 6);
	for (int index = 0; index < 6; index++) {
		memcpy(final_pixels + (index * single_image_size), loaded_images[index].pixels, single_image_size);
	}

	RhiTexture result = vulkan_renderer_texture_create(
		state.context,
		width, height,
		TEXTURE_TYPE_CUBE, TEXTURE_FORMAT_RGBA8_SRGB,
		TEXTURE_USAGE_SAMPLED, final_pixels);

	arena_release_scratch(scratch);
	return result;
}
