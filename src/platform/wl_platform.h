#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <wayland-client-core.h>

typedef struct wl_display *(*PFN_wl_display_connect)(const char *);
typedef void (*PFN_wl_display_disconnect)(struct wl_display *);

typedef int (*PFN_wl_display_flush)(struct wl_display *display);
typedef int (*PFN_wl_display_get_fd)(struct wl_display *);

typedef int (*PFN_wl_display_prepare_read)(struct wl_display *);
typedef void (*PFN_wl_display_cancel_read)(struct wl_display *display);

typedef int (*PFN_wl_display_dispatch)(struct wl_display *);
typedef int (*PFN_wl_display_dispatch_pending)(struct wl_display *display);
typedef int (*PFN_wl_display_read_events)(struct wl_display *display);
typedef int (*PFN_wl_display_roundtrip)(struct wl_display *);

typedef struct wl_proxy *(*PFN_wl_proxy_marshal_constructor)(struct wl_proxy *, uint32_t, const struct wl_interface *, ...);
typedef struct wl_proxy *(*PFN_wl_proxy_marshal_constructor_versioned)(struct wl_proxy *, uint32_t, const struct wl_interface *, uint32_t, ...);
typedef void (*PFN_wl_proxy_marshal)(struct wl_proxy *, uint32_t, ...);
typedef struct wl_proxy *(*PFN_wl_proxy_marshal_flags)(struct wl_proxy *, uint32_t, const struct wl_interface *, uint32_t, uint32_t, ...);
typedef int (*PFN_wl_proxy_add_listener)(struct wl_proxy *, void (**)(void), void *);
typedef void (*PFN_wl_proxy_destroy)(struct wl_proxy *);

typedef void *(*PFN_wl_proxy_get_user_data)(struct wl_proxy *);
typedef void (*PFN_wl_proxy_set_user_data)(struct wl_proxy *, void *);

typedef const char *const *(*PFN_wl_proxy_get_tag)(struct wl_proxy *);
typedef void (*PFN_wl_proxy_set_tag)(struct wl_proxy *, const char *const *);

typedef uint32_t (*PFN_wl_proxy_get_version)(struct wl_proxy *);


typedef struct wl_platform {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_surface *surface;

	struct {
		void *handle;
		union {
			struct {
				PFN_wl_display_connect display_connect;
				PFN_wl_display_disconnect display_disconnect;

				PFN_wl_proxy_marshal_flags proxy_marshal_flags;
				PFN_wl_proxy_get_version proxy_get_version;

				PFN_wl_proxy_add_listener proxy_add_listener;
				PFN_wl_display_roundtrip display_roundtrip;

				PFN_wl_display_dispatch display_dispatch;
			} func;
			void **func_array;
		};
	};

	struct {
		struct wl_interface *registry_interface;
	} interface;
} WLPlatform;

typedef struct platform Platform;

#define PLATFORM_WAYLAND_LIBRARY_STATE WLPlatform wl;

bool platform_init_wayland(Platform *platform);

bool wl_platform_startup(Platform *platform);
void wl_platform_shutdown(Platform *platform);

void wl_platform_poll_events(Platform *platform);
bool wl_platform_should_close(Platform *platform);

void wl_platform_get_window_size(Platform *platform, uint32_t *width, uint32_t *height);
void wl_platform_get_framebuffer_size(Platform *platform, uint32_t *width, uint32_t *height);

struct VkSurfaceKHR_T;
struct VkInstance_T;

bool wl_platform_create_vulkan_surface(Platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface);
