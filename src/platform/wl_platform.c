#include "platform/wl_platform.h"
#include "platform/internal.h"

#include "core/logger.h"
#include <string.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#include <wayland-util.h>

#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include "wayland-client-protocol-code.h"
#include "xdg-shell-client-protocol-code.h"

#include <dlfcn.h>

#define LIBRARY_COUNT sizeof(struct wl_library) / sizeof(void *)

WLLibrary _wl_library = { 0 };

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name);

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial);
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial);

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove
};
static const struct xdg_surface_listener xdg_surface_listener = { xdg_surface_configure };
static const struct xdg_wm_base_listener xdg_wm_base_listener = { xdg_wm_base_ping };

bool platform_init_wayland(struct platform *platform) {
	void *handle = dlopen("libwayland-client.so.0", RTLD_LAZY);
	if (handle == NULL)
		return false;

	WLPlatform *wl_platform = &platform->internal->wl;
	_wl_library.handle = handle;

	*(void **)(&_wl_library.display_connect) = dlsym(handle, "wl_display_connect");
	*(void **)(&_wl_library.display_disconnect) = dlsym(handle, "wl_display_disconnect");

	*(void **)(&_wl_library.proxy_marshal_flags) = dlsym(handle, "wl_proxy_marshal_flags");
	*(void **)(&_wl_library.proxy_get_version) = dlsym(handle, "wl_proxy_get_version");

	*(void **)(&_wl_library.proxy_add_listener) = dlsym(handle, "wl_proxy_add_listener");
	*(void **)(&_wl_library.display_roundtrip) = dlsym(handle, "wl_display_roundtrip");

	*(void **)(&_wl_library.display_dispatch) = dlsym(handle, "wl_display_dispatch");

	void **array = &_wl_library.handle;
	for (uint32_t i = 0; i < LIBRARY_COUNT; i++) {
		if (array[i] == NULL)
			return false;
	}

	platform->internal->startup = wl_platform_startup;
	platform->internal->shutdown = wl_platform_shutdown;

	platform->internal->poll_events = wl_platform_poll_events;
	platform->internal->should_close = wl_platform_should_close;

	platform->internal->window_size = wl_platform_get_window_size;
	platform->internal->framebuffer_size = wl_platform_get_framebuffer_size;

	platform->internal->create_vulkan_surface = wl_platform_create_vulkan_surface;

	return true;
}

bool wl_platform_startup(struct platform *platform) {
	WLPlatform *wl = &platform->internal->wl;
	wl->display = _wl_library.display_connect(NULL);

	struct wl_display *display = platform->internal->wl.display;
	if (display == NULL) {
		LOG_ERROR("Failed to create wayland display");
		return false;
	}

	wl->registry = wl_display_get_registry(wl->display);

	if (wl->registry == NULL) {
		LOG_ERROR("Failed to create registry");
		return false;
	}

	wl_registry_add_listener(wl->registry, &registry_listener, wl);
	wl_display_roundtrip(wl->display);

	wl->surface = wl_compositor_create_surface(wl->compositor);

	if (wl->surface == NULL) {
		LOG_ERROR("Failed to create surface");
		return false;
	}

	wl->xdg.surface = xdg_wm_base_get_xdg_surface(wl->wm_base, wl->surface);
	xdg_surface_add_listener(wl->xdg.surface, &xdg_surface_listener, wl);

	wl->xdg.toplevel = xdg_surface_get_toplevel(wl->xdg.surface);

	xdg_toplevel_set_min_size(wl->xdg.toplevel, platform->width, platform->height);
	xdg_toplevel_set_min_size(wl->xdg.toplevel, platform->width, platform->height);

	wl_surface_commit(wl->surface);

	return true;
}

void wl_platform_shutdown(struct platform *platform) {
	WLPlatform *wl = &platform->internal->wl;
	wl_display_disconnect(wl->display);
}

void wl_platform_poll_events(struct platform *platform) {
	WLPlatform *wl = &platform->internal->wl;
	while (wl_display_dispatch(wl->display) != -1) {
		// Do events
	}
}
bool wl_platform_should_close(struct platform *platform) {
	return platform->should_close;
}

void wl_platform_get_window_size(struct platform *platform, uint32_t *width, uint32_t *height) {}
void wl_platform_get_framebuffer_size(struct platform *platform, uint32_t *width, uint32_t *height) {}

bool wl_platform_create_vulkan_surface(struct platform *platform, VkInstance instance, VkSurfaceKHR *surface) {
	VkWaylandSurfaceCreateInfoKHR surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
		.display = platform->internal->wl.display,
		.surface = platform->internal->wl.surface,
	};

	if (vkCreateWaylandSurfaceKHR(instance, &surface_create_info, NULL, surface) != VK_SUCCESS)
		return false;

	return true;
}

void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t _version) {
	WLPlatform *wl = (WLPlatform *)data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		wl->compositor = wl_registry_bind(wl->registry, name, &wl_compositor_interface, 6);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		wl->wm_base = wl_registry_bind(wl->registry, name, &xdg_wm_base_interface, 7);
		xdg_wm_base_add_listener(wl->wm_base, &xdg_wm_base_listener, wl);
	}
}

void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	WLPlatform *wl = (WLPlatform *)data;

	xdg_surface_ack_configure(wl->xdg.surface, serial);
	wl_surface_commit(wl->surface);
}
void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) { xdg_wm_base_pong(xdg_wm_base, serial); }
