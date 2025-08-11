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

// TODO: Temp solution, make more robust
void *platform_window_handle(Platform *platform);
void *platform_instance_handle(Platform *platform);
