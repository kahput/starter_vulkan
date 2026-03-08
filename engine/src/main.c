#include "assets/importer.h"
#include "game_interface.h"
#include "input/input_types.h"
#include "platform.h"
#include "renderer.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"
#include "event.h"
#include "input.h"
#include "assets.h"

#include "common.h"
#include "core/debug.h"
#include "core/arena.h"
#include "core/logger.h"
#include "core/astring.h"

#include "platform/filesystem.h"

#include <math.h>
#include "core/cmath.h"
#include "scene.h"

#include <dlfcn.h>
#include <string.h>
#include <time.h>

typedef struct {
	void *handle;
	uint64_t last_write;
} DynamicLibrary;

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

typedef struct engine {
	Arena memory;

	Window *display;
	VulkanContext *context;

	uint64_t start_time;
} Engine;

static Engine engine = { 0 };

typedef struct {
	RhiTexture white;

	RhiSampler linear;
	RhiSampler nearest;

	RhiShader shader;

	RhiBuffer global_buffer;

	RhiBuffer vbo;
} Renderer2D;

typedef struct {
	RhiBuffer buffer;
	RhiTexture texture;
} Sprite;

Renderer2D renderer_make(void);
bool frame_begin(Renderer2D *renderer);
void frame_end(Renderer2D *renderer);
Sprite sprite_make(Renderer2D *renderer, String path);
void texture_draw(Renderer2D *renderer, Sprite sprite, Vector3f position, Vector3f scale, Vector3f tint);

int main(void) {
	logger_set_level(LOG_LEVEL_DEBUG);

	platform_startup();
	engine = (Engine){
		.memory = arena_make(MiB(512)),
	};

	engine.display = window_make(&engine.memory, 1280, 720, slit("test"));
	if (vulkan_renderer_make(&engine.memory, engine.display, &engine.context) == false) {
		LOG_ERROR("Failed to create vulkan context");
		return 1;
	}

	window_set_cursor_locked(engine.display, true);

	GameContext game_context = {
		.permanent_memory = arena_push(&engine.memory, MiB(32), 1, true),
		.permanent_memory_size = MiB(32),
		.vk_context = engine.context,
	};
	game_load(&game_context);

	Renderer2D renderer = renderer_make();
	Sprite sprite0 = sprite_make(&renderer, slit("assets/sprites/kenney/tile_0085.png"));
	Sprite sprite1 = sprite_make(&renderer, slit("assets/sprites/kenney/tile_0086.png"));

	float delta_time = 0.0f;
	float last_frame = 0.0f;

	while (window_is_open(engine.display)) {
		double time = platform_time();
		delta_time = time - last_frame;
		last_frame = time;

		uint64_t current_write_time = filesystem_last_modified(slit("libgame.so"));
		if (current_write_time && current_write_time != game_library.last_write) {
			LOG_INFO("Game file change detected. Reloading...");
			struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
			nanosleep(&ts, NULL);
			game_load(&game_context);
		}

		if (frame_begin(&renderer)) {
			texture_draw(&renderer, sprite1, (Vector3f){ .x = 0.0f, 0.0f, 0.0f }, (Vector3f){ 1.0f, 1.f, 1.0f }, (Vector3f){ 0.0f, 1.0f, 1.0f });
			texture_draw(&renderer, sprite0, (Vector3f){ .x = 0.0f, 0.0f, 0.0f }, (Vector3f){ 1.0f, 1.f, 1.0f }, (Vector3f){ 1.0f, 1.0f, 0.0f });

			frame_end(&renderer);
		}

		game_on_update_and_render(&game_context, delta_time);

		window_poll_events(engine.display);
	}

	vulkan_renderer_destroy(engine.context);

	return 0;
}

Renderer2D renderer_make(void) {
	Renderer2D result = { 0 };

	uint8_t WHITE[4] = { 255, 255, 255, 255 };
	result.white = vulkan_texture_make(engine.context, 1, 1, TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8, TEXTURE_USAGE_SAMPLED, WHITE);
	result.linear = vulkan_sampler_make(engine.context, LINEAR_SAMPLER);
	result.nearest = vulkan_sampler_make(engine.context, NEAREST_SAMPLER);

	ArenaTemp scratch = arena_scratch(NULL);

	result.shader = vulkan_shader_make(
		scratch.arena,
		engine.context,
		importer_load_shader(scratch.arena, slit("assets/shaders/unlit.vert.spv"), slit("assets/shaders/unlit.frag.spv")),
		NULL);
	arena_scratch_release(scratch);

	result.global_buffer = vulkan_buffer_make(engine.context, BUFFER_TYPE_UNIFORM, sizeof(Matrix4f) * 2, NULL);

	// clang-format off
    float vertices[] = { 
        // pos      // tex
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 
    
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 0.0f
    };
	// clang-format on
	result.vbo = vulkan_buffer_make(engine.context, BUFFER_TYPE_VERTEX, sizeof(vertices), vertices);

	return result;
}

bool frame_begin(Renderer2D *renderer) {
	Camera camera = {
		.projection = CAMERA_PROJECTION_PERSPECTIVE,
		.position = { 0.0f, 0.0f, -10.f },
		.target = { 0.0f, 0.0f, 0.0f },
		.up = { 0.0f, 1.0f, 0.0f },
		.fov = 45.f,
	};

	uint32_2 size = window_size(engine.display);
	if (vulkan_frame_begin(engine.context, size.x, size.y)) {
		DrawListDesc desc = {
			.color_attachments[0] = {
			  .clear = { .color = { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f } },
			},
			.color_attachment_count = 1
		};
		if (vulkan_drawlist_begin(engine.context, desc)) {
			Matrix4f projection = matrix4f_perspective(DEG2RAD(camera.fov), (float32)size.x / (float32)size.y, 0.01f, 1000.f);
			Matrix4f view = matrix4f_lookat(camera.position, camera.target, camera.up);
			vulkan_buffer_write(engine.context, renderer->global_buffer, 0, sizeof(Matrix4f), &view);
			vulkan_buffer_write(engine.context, renderer->global_buffer, sizeof(Matrix4f), sizeof(Matrix4f), &projection);

			PipelineDesc pipeline = DEFAULT_PIPELINE;
			pipeline.cull_mode = CULL_MODE_NONE;
			vulkan_shader_bind(engine.context, renderer->shader, pipeline);

			RhiUniformSet set0 = vulkan_uniformset_push(engine.context, renderer->shader, 0);
			vulkan_uniformset_bind_buffer(engine.context, set0, 0, renderer->global_buffer);
			vulkan_uniformset_bind(engine.context, set0);
			return true;
		} else
			return false;
	} else
		return false;
}
void frame_end(Renderer2D *renderer) {
	vulkan_drawlist_end(engine.context);
	vulkan_frame_end(engine.context);
}

Sprite sprite_make(Renderer2D *renderer, String path) {
	Sprite result = { 0 };

	ArenaTemp scratch = arena_scratch(NULL);
	ImageSource image = importer_load_image(scratch.arena, path);
	Vector4f tint = { 1.0f, 1.0f, 1.0f, 1.0f };

	result.texture = vulkan_texture_make(engine.context, image.width, image.height, TEXTURE_TYPE_2D, TEXTURE_FORMAT_RGBA8_SRGB, TEXTURE_USAGE_SAMPLED, image.pixels);
	result.buffer = vulkan_buffer_make(engine.context, BUFFER_TYPE_UNIFORM, sizeof(Vector4f), &tint);

	arena_scratch_release(scratch);

	return result;
}

void texture_draw(Renderer2D *renderer, Sprite sprite, Vector3f position, Vector3f scale, Vector3f tint) {
	Matrix4f transform = matrix4f_translated(position);
	transform = matrix4f_scale(transform, scale);

	vulkan_push_constants(engine.context, 0, sizeof(Matrix4f), &transform);

	RhiUniformSet set1 = vulkan_uniformset_push(engine.context, renderer->shader, 1);
	vulkan_uniformset_bind_buffer(engine.context, set1, 0, sprite.buffer);
	vulkan_uniformset_bind_texture(engine.context, set1, 1, sprite.texture, renderer->nearest);
	vulkan_buffer_write(engine.context, sprite.buffer, 0, sizeof(Vector3f), &tint);
	vulkan_uniformset_bind(engine.context, set1);

	vulkan_buffer_bind(engine.context, renderer->vbo, 0);
	vulkan_renderer_draw(engine.context, 6);
}

bool game_load(GameContext *context) {
	if (game_library.handle) {
		dlclose(game_library.handle);
		game_library.handle = NULL;
	}

	ArenaTemp scratch = arena_scratch(NULL);

	String path = slit("./libgame.so");
	String temp_path = string_pushf(scratch.arena, "./loaded_%s.so", "game");

	if (filesystem_file_copy(path, temp_path) == false) {
		LOG_ERROR("failed to copy file");
		ASSERT(false);
		arena_scratch_release(scratch);
		return false;
	}

	void *handle = dlopen(temp_path.memory, RTLD_NOW);
	if (handle == NULL) {
		LOG_ERROR("dlopen failed: %s", dlerror());
		ASSERT(false);
		arena_scratch_release(scratch);
		return false;
	}

	game_library.handle = handle;
	game_library.last_write = filesystem_last_modified(path);

	PFN_game_hookup hookup;
	*(void **)(&hookup) = dlsym(game_library.handle, GAME_HOOKUP_NAME);

	if (hookup == NULL) {
		LOG_ERROR("dlsym failed: %s", dlerror());
		ASSERT(false);
		arena_scratch_release(scratch);
		return false;
	}

	game = hookup();
	arena_scratch_release(scratch);

	return true;
}
