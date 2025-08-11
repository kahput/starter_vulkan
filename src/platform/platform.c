#include "platform.h"
#include "platform/internal.h"

#include "core/arena.h"
#include "core/logger.h"

#include <stdlib.h>
#include <string.h>

static const struct {
	bool (*initialize)(Platform *);
} supported_platforms[] = {
#if defined(PLATFORM_WIN32)
	{ platform_init_win32 },
#endif
#if defined(PLATFORM_WAYLAND)
	{ platform_init_wayland },
#endif
#if defined(PLATFORM_X11)
	{ platform_init_x11 },
#endif
};

Platform *platform_startup(Arena *arena, uint32_t width, uint32_t height, const char *title) {
	Platform *platform = arena_push_type(arena, Platform);

	size_t count = sizeof(supported_platforms) / sizeof(supported_platforms[0]);
	if (count == 0) {
		LOG_ERROR("No platform defined");
		return NULL;
	}

	uint32_t selected = 0;
#if defined(PLATFORM_WAYLAND) && defined(PLATFORM_X11)
	const char *const session = getenv("XDG_SESSION_TYPE");
	if (session) {
		if (strcmp(session, "wayland") == 0 && getenv("WAYLAND_DISPLAY")) {
			LOG_INFO("Wayland selected");
			selected = 0;
		} else if (strcmp(session, "x11") == 0 && getenv("DISPLAY")) {
			LOG_INFO("X11 selected");
			selected = 1;
		}
	}
#endif

	if (supported_platforms[selected].initialize(platform))
		return platform;
	else {
		LOG_ERROR("Failed to initialize platform");
		return NULL;
	}
}

void platform_shutdown(Platform *platform) {
	platform->internal->shutdown(platform);
}

void platform_poll_events(Platform *platform) {
	platform->internal->poll_events(platform);
}
bool platform_should_close(Platform *platform) {
	return platform->internal->should_close(platform);
}

void platform_get_window_size(Platform *platform, uint32_t *width, uint32_t *height) {
	platform->internal->window_size(platform, width, height);
}
void platform_get_framebuffer_size(Platform *platform, uint32_t *width, uint32_t *height) {
	platform->internal->framebuffer_size(platform, width, height);
}

// TODO: Temp solution, make more robust
void *platform_window_handle(Platform *platform) {
	return platform->internal->window_handle(platform);
}
void *platform_instance_handle(Platform *platform) {
	return platform->internal->instance_handle(platform);
}
