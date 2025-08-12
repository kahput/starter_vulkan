#pragma once

#include "core/arena.h"

#include <stdbool.h>

typedef struct platform_internal PlatformInternal;

typedef struct platform {
	uint32_t width, height;
	bool should_close;

	PlatformInternal *internal;
} Platform;

Platform *platform_startup(Arena *arena, uint32_t width, uint32_t height, const char *title);
void platform_shutdown(Platform *platform);

void platform_poll_events(Platform *platform);
bool platform_should_close(Platform *platform);

void platform_get_window_size(Platform *platform, uint32_t *width, uint32_t *height);
void platform_get_framebuffer_size(Platform *platform, uint32_t *width, uint32_t *height);

struct VkSurfaceKHR_T;
struct VkInstance_T;

bool platform_create_vulkan_surface(Platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface);
