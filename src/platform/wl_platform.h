#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct wl_platform {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_surface *surface;
} WLPlatform;

typedef struct platform Platform;

#define PLATFORM_WAYLAND_LIBRARY_STATE WLPlatform wl;

bool platform_init_wayland(Platform* platform);

bool wl_platform_startup(Platform *platform);
void wl_platform_shutdown(Platform *platform);

void wl_platform_poll_events(Platform *platform);
bool wl_platform_should_close(Platform *platform);

void wl_platform_get_window_size(Platform *platform, uint32_t *width, uint32_t *height);
void wl_platform_get_framebuffer_size(Platform *platform, uint32_t *width, uint32_t *height);

// TODO: Temp solution, make more robust
void *wl_platform_window_handle(Platform *platform);
void *wl_platform_instance_handle(Platform *platform);
