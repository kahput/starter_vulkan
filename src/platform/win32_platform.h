#pragma once

#include "common.h"

#ifdef PLATFORM_WINDOWS
#include <windows.h>
typedef struct win32_platform {
	HWND instance;
} Win32Platform;

#define PLATFORM_WAYLAND_LIBRARY_STATE Win32Platform win32;

struct platform;

bool platform_init_win32(Platform *platform);

bool win32_platform_startup(Platform *platform);
void win32_platform_shutdown(Platform *platform);

void win32_platform_poll_events(Platform *platform);
bool win32_platform_should_close(Platform *platform);

void win32_platform_get_window_size(Platform *platform, uint32_t *width, uint32_t *height);
void win32_platform_get_framebuffer_size(Platform *platform, uint32_t *width, uint32_t *height);

struct VkSurfaceKHR_T;
struct VkInstance_T;

bool win32_platform_create_vulkan_surface(Platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface);
#endif
