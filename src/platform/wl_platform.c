#include "platform/wl_platform.h"
#include "platform/internal.h"

#include "core/logger.h"

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#include <wayland-client-protocol.h>

#include <dlfcn.h>
#include <time.h>

#define FUNC_COUNT sizeof(((WLPlatform *)0)->func) / sizeof(void *)

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	LOG_INFO("Global: { interface = %s, name = %d, version = %d}", interface, name, version);
}
static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove
};

bool platform_init_wayland(Platform *platform) {
	void *handle = dlopen("libwayland-client.so.0", RTLD_LAZY);
	if (handle == NULL)
		return false;

	platform->internal->wl.handle = handle;

	platform->internal->wl.func.display_connect = (PFN_wl_display_connect)dlsym(handle, "wl_display_connect");
	platform->internal->wl.func.display_disconnect = (PFN_wl_display_disconnect)dlsym(handle, "wl_display_disconnect");

	platform->internal->wl.func.proxy_marshal_flags = (PFN_wl_proxy_marshal_flags)dlsym(handle, "wl_proxy_marshal_flags");
	platform->internal->wl.func.proxy_get_version = (PFN_wl_proxy_get_version)dlsym(handle, "wl_proxy_get_version");

	platform->internal->wl.func.proxy_add_listener = (PFN_wl_proxy_add_listener)dlsym(handle, "wl_proxy_add_listener");
	platform->internal->wl.func.display_roundtrip = (PFN_wl_display_roundtrip)dlsym(handle, "wl_display_roundtrip");

	platform->internal->wl.func.display_dispatch = (PFN_wl_display_dispatch)dlsym(handle, "wl_display_dispatch");

	platform->internal->wl.interface.registry_interface = dlsym(handle, "wl_registry_interface");

	for (uint32_t i = 0; i < 10; i++) {
		if (platform->internal->wl.func_array[i] == NULL) {
			return false;
		}
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

bool wl_platform_startup(Platform *platform) {
	platform->internal->wl.display = platform->internal->wl.func.display_connect(NULL);

	struct wl_display *display = platform->internal->wl.display;
	if (display == NULL) {
		LOG_ERROR("Failed to create wayland display");
		return false;
	}
	uint32_t version = platform->internal->wl.func.proxy_get_version((struct wl_proxy *)display);

	platform->internal->wl.registry = (struct wl_registry *)platform->internal->wl.func.proxy_marshal_flags(
		(struct wl_proxy *)display,
		WL_DISPLAY_GET_REGISTRY,
		platform->internal->wl.interface.registry_interface,
		version,
		0, NULL);

	struct wl_registry *registry = platform->internal->wl.registry;
	if (registry == NULL) {
		LOG_ERROR("Failed to create registry");
		goto registry_failed;
	}

	platform->internal->wl.func.proxy_add_listener((struct wl_proxy *)registry, (void (**)(void))&registry_listener, NULL);
	platform->internal->wl.func.display_roundtrip(display);

registry_failed:
	return true;
}

void wl_platform_shutdown(Platform *platform) {
	platform->internal->wl.func.display_disconnect(platform->internal->wl.display);
}

void wl_platform_poll_events(Platform *platform) {
	struct wl_display *display = platform->internal->wl.display;
	while (platform->internal->wl.func.display_dispatch(display) != -1) {
		// Do events
	}
}
bool wl_platform_should_close(Platform *platform) {
	return false;
}

void wl_platform_get_window_size(Platform *platform, uint32_t *width, uint32_t *height) {}
void wl_platform_get_framebuffer_size(Platform *platform, uint32_t *width, uint32_t *height) {}

bool wl_platform_create_vulkan_surface(Platform *platform, VkInstance instance, VkSurfaceKHR *surface) {
	VkWaylandSurfaceCreateInfoKHR surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
		.display = platform->internal->wl.display,
		.surface = platform->internal->wl.surface,
	};

	if (vkCreateWaylandSurfaceKHR(instance, &surface_create_info, NULL, surface) != VK_SUCCESS)
		return false;

	return true;
}
