#pragma once

#include "common.h"

#include "core/arena.h"

typedef struct platform {
	uint32_t logical_width, logical_height;
	uint32_t physical_width, physical_height;
	bool should_close;

	void *internal;
} Platform;

typedef void (*fn_platform_dimensions)(struct platform *platform, uint32_t width, uint32_t height);

typedef enum {
	PLATFORM_POINTER_NORMAL,
	PLATFORM_POINTER_DISABLED,
} PointerMode;

bool platform_startup(Arena *arena, uint32_t width, uint32_t height, const char *title, Platform *platform);
void platform_shutdown(Platform *platform);

void platform_poll_events(Platform *platform);
bool platform_should_close(Platform *platform);

void platform_get_logical_dimensions(Platform *platform, uint32_t *width, uint32_t *height);
void platform_get_physical_dimensions(Platform *platform, uint32_t *width, uint32_t *height);

bool platform_pointer_mode(Platform *platform, PointerMode mode);

double platform_time_seconds(Platform *platform);
uint64_t platform_time_ms(Platform *platform);
uint64_t platform_random_64(Platform *platform);

struct VkSurfaceKHR_T;
struct VkInstance_T;

bool platform_create_vulkan_surface(Platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface);
const char **platform_vulkan_extensions(Platform *platform, uint32_t *count);
