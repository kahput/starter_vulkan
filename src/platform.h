#pragma once

#include "core/arena.h"

#include <stdbool.h>

typedef struct platform {
	uint32_t logical_width, logical_height;
	uint32_t physical_width, physical_height;
	bool should_close;

	void *internal;
} Platform;


typedef void (*PFN_platform_dimensions)(struct platform *platform, uint32_t width, uint32_t height);

Platform *platform_startup(Arena *arena, uint32_t width, uint32_t height, const char *title);
void platform_shutdown(Platform *platform);

void platform_poll_events(Platform *platform);
bool platform_should_close(Platform *platform);

void platform_logical_dimensions(Platform *platform, uint32_t *width, uint32_t *height);
void platform_physical_dimensions(Platform *platform, uint32_t *width, uint32_t *height);

void platform_logical_dimensions_set_callback(struct platform *platform, PFN_platform_dimensions dimensions);
void platform_physical_dimensions_set_callback(struct platform *platform, PFN_platform_dimensions dimensions);

uint64_t platform_time_ms(Platform *platform);

struct VkSurfaceKHR_T;
struct VkInstance_T;

bool platform_create_vulkan_surface(Platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface);
const char **platform_vulkan_extensions(Platform *platform, uint32_t *count);
