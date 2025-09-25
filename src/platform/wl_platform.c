#include "platform/wl_platform.h"
#include "platform/internal.h"

#include "core/logger.h"
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#include <wayland-util.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include "fractional-scale-v1-client-protocol-code.h"
#include "viewporter-client-protocol-code.h"
#include "wayland-client-protocol-code.h"
#include "xdg-shell-client-protocol-code.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define LIBRARY_COUNT sizeof(struct wl_library) / sizeof(void *)
WLLibrary _wl_library = { 0 };

static const char *extensions[] = {
	VK_KHR_SURFACE_EXTENSION_NAME,
	VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static const struct wl_registry_listener registry_listener = { .global = registry_handle_global, .global_remove = registry_handle_global_remove };

static void wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial);
static const struct xdg_wm_base_listener wm_base_listener = { .ping = wm_base_ping };

static void surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial);
static const struct xdg_surface_listener surface_listener = { .configure = surface_configure };

static void toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states);
static void toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel);
static void toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height);
static void toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities);
static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
	.configure_bounds = toplevel_configure_bounds,
	.wm_capabilities = toplevel_wm_capabilities,
};

static void preferred_scale(void *data, struct wp_fractional_scale_v1 *wp_fractional_scale_v1, uint32_t scale);
static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
	.preferred_scale = preferred_scale,
};

static struct wl_buffer *
create_shm_buffer(struct platform *platform);

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
	platform->internal->wl = (WLPlatform){ 0 };

	platform->internal->startup = wl_startup;
	platform->internal->shutdown = wl_shutdown;

	platform->internal->poll_events = wl_poll_events;
	platform->internal->should_close = wl_should_close;

	platform->internal->logical_dimensions = wl_get_logical_dimensions;
	platform->internal->physical_dimensions = wl_get_physical_dimensions;

	platform->internal->create_vulkan_surface = wl_create_vulkan_surface;
	platform->internal->vulkan_extensions = wl_vulkan_extensions;

	return true;
}

bool wl_startup(struct platform *platform) {
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

	wl->viewport = wp_viewporter_get_viewport(wl->viewporter, wl->surface);
	wl->xdg.surface = xdg_wm_base_get_xdg_surface(wl->wm_base, wl->surface);
	xdg_surface_add_listener(wl->xdg.surface, &surface_listener, platform);

	wl->xdg.toplevel = xdg_surface_get_toplevel(wl->xdg.surface);
	// xdg_toplevel_set_fullscreen(wl->xdg.toplevel, wl->output);
	// xdg_surface_set_window_geometry(wl->xdg.surface, 0, 0, platform->width, platform->height)
	xdg_toplevel_set_min_size(wl->xdg.toplevel, platform->logical_width, platform->logical_height);
	xdg_toplevel_set_max_size(wl->xdg.toplevel, platform->logical_width, platform->logical_height);

	xdg_toplevel_add_listener(wl->xdg.toplevel, &toplevel_listener, platform);

	wl->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(wl->fractional_scale_manager, wl->surface);
	wp_fractional_scale_v1_add_listener(wl->fractional_scale, &fractional_scale_listener, platform);

	wl_surface_commit(wl->surface);

	// Attach buffer in configure callback
	wl_display_roundtrip(wl->display);

	// Retrived surface fractional scaling from attached buffer
	wl_display_roundtrip(wl->display);

	return true;
}

void wl_shutdown(struct platform *platform) {
	WLPlatform *wl = &platform->internal->wl;
	wl_display_disconnect(wl->display);
}

void wl_poll_events(struct platform *platform) {
	WLPlatform *wl = &platform->internal->wl;
	wl_display_roundtrip(wl->display);
}
bool wl_should_close(struct platform *platform) {
	return platform->should_close;
}

void wl_get_logical_dimensions(struct platform *platform, uint32_t *width, uint32_t *height) {}
void wl_get_physical_dimensions(struct platform *platform, uint32_t *width, uint32_t *height) {}

bool wl_create_vulkan_surface(struct platform *platform, VkInstance instance, VkSurfaceKHR *surface) {
	WLPlatform *wl = &platform->internal->wl;
	wl->use_vulkan = true;

	LOG_INFO("Using vulkan for presentation now");

	VkWaylandSurfaceCreateInfoKHR surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
		.display = platform->internal->wl.display,
		.surface = platform->internal->wl.surface,
	};

	if (vkCreateWaylandSurfaceKHR(instance, &surface_create_info, NULL, surface) != VK_SUCCESS)
		return false;

	return true;
}

const char **wl_vulkan_extensions(struct platform *platform, uint32_t *count) {
	*count = sizeof(extensions) / sizeof(*extensions);
	return extensions;
}

void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t _version) {
	WLPlatform *wl = (WLPlatform *)data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		wl->compositor = wl_registry_bind(wl->registry, name, &wl_compositor_interface, 6);
	else if (strcmp(interface, wl_output_interface.name) == 0)
		wl->output = wl_registry_bind(wl->registry, name, &wl_output_interface, 2);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		wl->shm = wl_registry_bind(wl->registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		wl->wm_base = wl_registry_bind(wl->registry, name, &xdg_wm_base_interface, 7);
		xdg_wm_base_add_listener(wl->wm_base, &wm_base_listener, wl);
	} else if (strcmp(interface, wp_viewporter_interface.name) == 0)
		wl->viewporter = wl_registry_bind(wl->registry, name, &wp_viewporter_interface, 1);
	else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0)
		wl->fractional_scale_manager = wl_registry_bind(wl->registry, name, &wp_fractional_scale_manager_v1_interface, 1);
}
void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

void surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	struct platform *platform = (struct platform *)data;
	WLPlatform *wl = &platform->internal->wl;

	xdg_surface_ack_configure(wl->xdg.surface, serial);

	if (wl->use_vulkan == false) {
		wl_surface_attach(wl->surface, create_shm_buffer(platform), 0, 0);
		wl_surface_commit(wl->surface);
	}
}
void wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) { xdg_wm_base_pong(xdg_wm_base, serial); }

void toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {
	struct platform *platform = (struct platform *)data;
	WLPlatform *wl = &platform->internal->wl;

	if (width && height) {
		platform->logical_width = width, platform->logical_height = height;
	}

	if (wl->viewport) {
		wp_viewport_set_destination(wl->viewport, platform->logical_width, platform->logical_height);
	}
}
void toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
	struct platform *platform = (struct platform *)data;
	platform->should_close = true;
}
void toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height) {}
void toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities) {}

void preferred_scale(void *data, struct wp_fractional_scale_v1 *wp_fractional_scale_v1, uint32_t scale) {
	struct platform *platform = (struct platform *)data;
	float fractional_scale = (float)scale / 120.f;
	platform->physical_width = platform->logical_width * fractional_scale, platform->physical_height = platform->logical_height * fractional_scale;
}

static int create_anonymous_file(off_t size) {
	char template[] = "/tmp/wayland-shm-XXXXXX";
	int fd = mkstemp(template);
	if (fd < 0)
		return -1;

	unlink(template); // we don’t need the file on disk
	if (ftruncate(fd, size) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

struct wl_buffer *create_shm_buffer(struct platform *platform) {
	const int stride = platform->logical_width * 4;
	const int length = platform->logical_width * platform->logical_height * 4;

	const int fd = create_anonymous_file(length);
	if (fd < 0) {
		LOG_ERROR("Wayland: Failed to create buffer file of size %d: %s", strerror(errno));
		return NULL;
	}

	void *data = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		LOG_ERROR("Wayland: Failed to map file: %s", strerror(errno));
		close(fd);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(platform->internal->wl.shm, fd, length);

	close(fd);

	unsigned char *target = data;

	/* Draw checkerboxed background */
	for (uint32_t i = 0; i < platform->logical_width * platform->logical_height; i++) {
		*target++ = (unsigned char)0;
		*target++ = (unsigned char)0;
		*target++ = (unsigned char)0;
		*target++ = (unsigned char)255;
	}

	struct wl_buffer *buffer =
		wl_shm_pool_create_buffer(pool, 0,
			platform->logical_width,
			platform->logical_height,
			stride, WL_SHM_FORMAT_ARGB8888);
	munmap(data, length);
	wl_shm_pool_destroy(pool);

	return buffer;
}
