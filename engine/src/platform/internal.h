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

	void (*logical_dimensions)(Platform *, uint32_t *, uint32_t *);
	void (*physical_dimensions)(Platform *, uint32_t *, uint32_t *);

	bool (*pointer_mode)(Platform *, PointerMode);

	double (*time)(Platform *);
	uint64_t (*time_ms)(Platform *);
	uint64_t (*random_64)(Platform *);

	bool (*create_vulkan_surface)(Platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface);
	const char **(*vulkan_extensions)(uint32_t *count);

	PLATFORM_LIBRARY_STATE
};
