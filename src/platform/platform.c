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
#if defined(PLATFORM_X11)
	{ platform_init_x11 },
#endif
#if defined(PLATFORM_WAYLAND)
	{ platform_init_wayland },
#endif
};

Platform *platform_startup(Arena *arena, uint32_t width, uint32_t height, const char *title) {
	Platform *platform = arena_push_type(arena, Platform);
	platform->internal = arena_push_type(arena, struct platform_internal);

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
			selected = 1;
		}
	}
#endif

	if (supported_platforms[selected].initialize(platform) == false) {
		LOG_ERROR("Failed to initialize platform");
		return NULL;
	}

	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	internal->ID = selected;
	platform->logical_width = width, platform->logical_height = height;
	platform->physical_width = width, platform->physical_height = height;
	internal->startup(platform);

	return platform;
}

void platform_shutdown(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	internal->shutdown(platform);
}

void platform_poll_events(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	internal->poll_events(platform);
}
bool platform_should_close(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	return internal->should_close(platform);
}

void platform_get_logical_dimensions(Platform *platform, uint32_t *width, uint32_t *height) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	internal->logical_dimensions(platform, width, height);
}
void platform_get_physical_dimensions(Platform *platform, uint32_t *width, uint32_t *height) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	internal->physical_dimensions(platform, width, height);
}

uint64_t platform_time_ms(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	return internal->time_ms(platform);
}

void platform_logical_dimensions_set_callback(struct platform *platform, PFN_platform_dimensions dimensions) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	internal->set_logical_dimensions_callback(platform, dimensions);
}
void platform_physical_dimensions_set_callback(struct platform *platform, PFN_platform_dimensions dimensions) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	internal->set_physical_dimensions_callback(platform, dimensions);
}

bool platform_create_vulkan_surface(Platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	return internal->create_vulkan_surface(platform, instance, surface);
}

const char **platform_vulkan_extensions(Platform *platform, uint32_t *count) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	return internal->vulkan_extensions(platform, count);
}
