#include "platform/wl_platform.h"
#include "platform/internal.h"

#include <dlfcn.h>

bool platform_init_wayland(Platform *platform) {
	void *handle = dlopen("libwayland-client.so.0", RTLD_LAZY);
	if (handle)
		return true;
	return false;
}

bool wl_platform_startup(Platform *platform) {
	return true;
}

void wl_platform_shutdown(Platform *platform);

void wl_platform_poll_events(Platform *platform);
bool wl_platform_should_close(Platform *platform);

void wl_platform_get_window_size(Platform *platform, uint32_t *width, uint32_t *height);
void wl_platform_get_framebuffer_size(Platform *platform, uint32_t *width, uint32_t *height);

// TODO: Temp solution, make more robust
void *wl_platform_window_handle(Platform *platform);
void *wl_platform_instance_handle(Platform *platform);
