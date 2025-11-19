#pragma once

#include "common.h"
#include "platform.h"

#include <wayland-client-core.h>

typedef struct wl_display *(*PFN_wl_display_connect)(const char *);
typedef void (*PFN_wl_display_disconnect)(struct wl_display *);

typedef struct wl_proxy *(*PFN_wl_proxy_marshal_flags)(struct wl_proxy *, uint32_t, const struct wl_interface *, uint32_t, uint32_t, ...);
typedef uint32_t (*PFN_wl_proxy_get_version)(struct wl_proxy *);

typedef int (*PFN_wl_proxy_add_listener)(struct wl_proxy *, void (**)(void), void *);
typedef int (*PFN_wl_display_roundtrip)(struct wl_display *);

typedef int (*PFN_wl_display_dispatch)(struct wl_display *);

typedef struct wl_library {
	void *handle;

	PFN_wl_display_connect display_connect;
	PFN_wl_display_disconnect display_disconnect;

	PFN_wl_proxy_marshal_flags proxy_marshal_flags;
	PFN_wl_proxy_get_version proxy_get_version;

	PFN_wl_proxy_add_listener proxy_add_listener;
	PFN_wl_display_roundtrip display_roundtrip;

	PFN_wl_display_dispatch display_dispatch;

} WLLibrary;
extern WLLibrary _wl_library;

#define wl_display_connect _wl_library.display_connect
#define wl_display_disconnect _wl_library.display_disconnect

#define wl_proxy_marshal_flags _wl_library.proxy_marshal_flags
#define wl_proxy_get_version _wl_library.proxy_get_version

#define wl_proxy_add_listener _wl_library.proxy_add_listener
#define wl_display_roundtrip _wl_library.display_roundtrip

#define wl_display_dispatch _wl_library.display_dispatch

struct platform;

typedef struct wl_platform {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_output *output;
	struct wl_shm *shm;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_surface *surface;
	struct xdg_wm_base *wm_base;

	struct wp_viewporter *viewporter;
	struct wp_viewport *viewport;
	struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
	struct wp_fractional_scale_v1 *fractional_scale;
	float scale_factor;

	struct {
		struct xdg_surface *surface;
		struct xdg_toplevel *toplevel;
	} xdg;

	struct {
		fn_platform_dimensions logical_size;
		fn_platform_dimensions physical_size;
	} callback;

	bool use_vulkan;
} WLPlatform;

#define PLATFORM_WAYLAND_LIBRARY_STATE WLPlatform wl;

bool platform_init_wayland(Platform *platform);

bool wl_startup(Platform *platform);
void wl_shutdown(Platform *platform);

void wl_poll_events(Platform *platform);
bool wl_should_close(Platform *platform);

void wl_get_logical_dimensions(Platform *platform, uint32_t *width, uint32_t *height);
void wl_get_physical_dimensions(Platform *platform, uint32_t *width, uint32_t *height);

uint64_t wl_time_ms(Platform *platform);

void wl_set_logical_dimensions_callback(Platform *platform, fn_platform_dimensions callback);
void wl_set_physical_dimensions_callback(Platform *platform, fn_platform_dimensions callback);

struct VkSurfaceKHR_T;
struct VkInstance_T;

bool wl_create_vulkan_surface(Platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface);
const char **wl_vulkan_extensions(Platform *platform, uint32_t *count);
