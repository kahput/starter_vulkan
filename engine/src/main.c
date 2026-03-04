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

	window_set_fullscreen(engine.display, true);
	window_set_cursor_locked(engine.display, true);

	float delta_time = 0.0f;
	float last_frame = 0.0f;

	GameContext game_context = {
		.permanent_memory = arena_push(&engine.memory, MiB(32), 1, true),
		.permanent_memory_size = MiB(32),
		.vk_context = engine.context,
	};

	game_load(&game_context);

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

		game_on_update_and_render(&game_context, delta_time);

		window_poll_events(engine.display);
	}

	vulkan_renderer_destroy(engine.context);

	return 0;
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

	if (hookup == false) {
		LOG_ERROR("dlsym failed: %s", dlerror());
		ASSERT(false);
		arena_scratch_release(scratch);
		return false;
	}

	game = hookup();
	arena_scratch_release(scratch);

	return true;
}
