#pragma once

#include "platform.h"

#if defined(PLATFORM_WIN32)
#include "platform/win32_platform.h"
#else
#define PLATFORM_WIN32_LIBRARY_STATE
#endif

#if defined(PLATFORM_WAYLAND)
#include "platform/wl_platform.h"
#else
#define PLATFORM_WAYLAND_LIBRARY_STATE
#endif

#if defined(PLATFORM_X11)
#include "platform/x11_platform.h"
#else
#define PLATFORM_X11_LIBRARY_STATE
#endif

#define PLATFORM_LIBRARY_STATE     \
	PLATFORM_WAYLAND_LIBRARY_STATE \
	PLATFORM_X11_LIBRARY_STATE     \
	PLATFORM_WIN32_LIBRARY_STATE

struct platform_internal {
	uint32_t ID;
	bool (*startup)(Platform *platform);
	void (*shutdown)(Platform *platform);

	void (*poll_events)(Platform *);
	bool (*should_close)(Platform *);

	void (*window_size)(Platform *, uint32_t *, uint32_t *);
	void (*framebuffer_size)(Platform *, uint32_t *, uint32_t *);

	void *(*window_handle)(Platform *);
	void *(*instance_handle)(Platform *);

	bool (*create_vulkan_surface)(Platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface);

	PLATFORM_LIBRARY_STATE
};
