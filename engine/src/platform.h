#pragma once

#include "common.h"
#include "core/arena.h"
#include "core/strings.h"

#include "events/platform_events.h"

bool platform_startup(void);
void platform_shutdown(void);

double platform_time(void);
ENGINE_API uint64_t platform_time_absolute(void);
void platform_sleep(uint32_t ms);

typedef struct window Window;
Window *window_make(Arena *arena, uint32_t width, uint32_t height, String title);
void window_poll_events(Window *window);
bool window_is_open(Window *window);

ENGINE_API uint2 window_size(Window *window);
ENGINE_API uint2 window_size_pixel(Window *window);

void window_set_callback(PFN_event_handler handler);

void window_set_fullscreen(Window *window, bool fullscreen);
bool window_is_fullscreen(Window *window);
void window_set_title(Window *window, String title);
void window_set_cursor_visible(Window *window, bool visible);
void window_set_cursor_locked(Window *window, bool locked);

struct VkInstance_T;
struct VkSurfaceKHR_T;
struct VkAllocationCallbacks;

const char **platform_vulkan_extensions(uint32_t *count);
bool window_vulkan_surface_make(Window *window, struct VkInstance_T *instance, struct VkSurfaceKHR_T **out_surface);
