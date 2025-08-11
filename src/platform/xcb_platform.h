#pragma once

#include <xcb/randr.h>
#include <xcb/xcb.h>

#include <stdbool.h>
#include <stdint.h>

typedef xcb_connection_t *PFN_xcb_connect(const char *displayname, int *screenp);

typedef struct xcb_platform {
	xcb_connection_t *connection;
	xcb_window_t window;

	struct {
		PFN_xcb_connect *xcb_connect;
	} API;
} XCBPlatform;

typedef struct platform Platform;

#define PLATFORM_X11_LIBRARY_STATE XCBPlatform xcb;

bool platform_init_x11(Platform *platform);

bool xcb_platform_startup(Platform *platform);
void xcb_platform_shutdown(Platform *platform);

void xcb_platform_poll_events(Platform *platform);
bool xcb_platform_should_close(Platform *platform);

void xcb_platform_get_window_size(Platform *platform, uint32_t *width, uint32_t *height);
void xcb_platform_get_framebuffer_size(Platform *platform, uint32_t *width, uint32_t *height);

// TODO: Temp solution, make more robust
void *xcb_platform_window_handle(Platform *platform);
void *xcb_platform_instance_handle(Platform *platform);
