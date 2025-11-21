#include "platform/internal.h"

#include "core/logger.h"
#include <xcb/xproto.h>

#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>

#include "common.h"
#include <dlfcn.h>

#define XCB_COUNT sizeof(((X11Platform *)0)->xcb) / sizeof(void *)

static const char *extensions[] = {
	"VK_KHR_surface",
	"VK_KHR_xcb_surface"
};

bool platform_init_x11(Platform *platform) {
	void *handle = dlopen("libxcb.so", RTLD_LAZY);
	if (handle == NULL)
		return false;

	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	x11->xcb.handle = handle;

	// CONNECTION
	*(void **)(&x11->xcb.connect) = dlsym(handle, "xcb_connect");
	*(void **)(&x11->xcb.disconnect) = dlsym(handle, "xcb_disconnect");

	// SCREEN
	*(void **)(&x11->xcb.get_setup) = dlsym(handle, "xcb_get_setup");
	*(void **)(&x11->xcb.setup_roots_iterator) = dlsym(handle, "xcb_setup_roots_iterator");

	// ATOM
	*(void **)(&x11->xcb.intern_atom) = dlsym(handle, "xcb_intern_atom");
	*(void **)(&x11->xcb.intern_atom_reply) = dlsym(handle, "xcb_intern_atom_reply");

	// WINDOW
	*(void **)(&x11->xcb.generate_id) = dlsym(handle, "xcb_generate_id");
	*(void **)(&x11->xcb.create_window) = dlsym(handle, "xcb_create_window");
	*(void **)(&x11->xcb.map_window) = dlsym(handle, "xcb_map_window");
	*(void **)(&x11->xcb.change_property) = dlsym(handle, "xcb_change_property");

	// GEMOTRY
	*(void **)(&x11->xcb.get_geometry) = dlsym(handle, "xcb_get_geometry");
	*(void **)(&x11->xcb.get_geometry_reply) = dlsym(handle, "xcb_get_geometry_reply");

	// EVENT
	*(void **)(&x11->xcb.poll_for_event) = dlsym(handle, "xcb_poll_for_event");
	*(void **)(&x11->xcb.flush) = dlsym(handle, "xcb_flush");

	void **array = &x11->xcb.handle;
	for (uint32_t i = 0; i < XCB_COUNT; i++) {
		if (array[i] == NULL) {
			return false;
		}
	}

	internal->startup = x11_startup;
	internal->shutdown = x11_shutdown;

	internal->poll_events = x11_poll_events;
	internal->should_close = x11_should_close;

	internal->time_ms = x11_time_ms;

	internal->logical_dimensions = x11_get_logical_dimensions;
	internal->physical_dimensions = x11_get_physical_dimensions;

	internal->set_logical_dimensions_callback = x11_set_logical_dimensions_callback;
	internal->set_physical_dimensions_callback = x11_set_physical_dimensions_callback;

	internal->create_vulkan_surface = x11_create_vulkan_surface;
	internal->vulkan_extensions = x11_vulkan_extensions;

	return true;
}

bool x11_startup(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	x11->connection = x11->xcb.connect(NULL, NULL);

	x11->window = x11->xcb.generate_id(x11->connection);

	xcb_screen_t *screen = x11->xcb.setup_roots_iterator(x11->xcb.get_setup(x11->connection)).data;

	xcb_intern_atom_cookie_t proto_cookie = x11->xcb.intern_atom(x11->connection, 1, 12, "WM_PROTOCOLS");
	xcb_intern_atom_cookie_t close_cookie = x11->xcb.intern_atom(x11->connection, 0, 16, "WM_DELETE_WINDOW");

	xcb_atom_t wm_protocols_atom = x11->xcb.intern_atom_reply(x11->connection, proto_cookie, 0)->atom;
	xcb_atom_t wm_delete_window_atom = x11->xcb.intern_atom_reply(x11->connection, close_cookie, 0)->atom;

	uint32_t event_mask =
		XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION |
		XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	uint32_t values[1] = {
		event_mask
	};
	x11->xcb.create_window(x11->connection, XCB_COPY_FROM_PARENT, x11->window, screen->root, 0, 0, platform->logical_width, platform->logical_height, 1, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_EVENT_MASK, values);

	x11->xcb.map_window(x11->connection, x11->window);

	x11->xcb.flush(x11->connection);

	x11->xcb.change_property(x11->connection, XCB_PROP_MODE_REPLACE, x11->window, wm_protocols_atom, XCB_ATOM_ATOM, 32, 1, &wm_delete_window_atom);

	return true;
}

void x11_shutdown(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	x11->xcb.disconnect(x11->connection);
}

void x11_poll_events(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	xcb_generic_event_t *event = NULL;
	while ((event = x11->xcb.poll_for_event(x11->connection))) {
		switch (event->response_type & ~0x80) {
			case XCB_CONFIGURE_NOTIFY: {
				xcb_configure_notify_event_t *cfg_event = (xcb_configure_notify_event_t *)event;

				uint16_t new_width = cfg_event->width, new_height = cfg_event->height;
				if (x11->callback.logical_size)
					x11->callback.logical_size(platform, new_width, new_height);
				if (x11->callback.physical_size)
					x11->callback.physical_size(platform, new_width, new_height);

			} break;
			case XCB_CLIENT_MESSAGE: {
				platform->should_close = true;
			} break;

			case XCB_KEY_PRESS: {
				xcb_key_press_event_t *key_press_event = (xcb_key_press_event_t *)event;
				LOG_TRACE("Key %d pressed", key_press_event->detail);
			} break;
			case XCB_KEY_RELEASE: {
				xcb_key_release_event_t *key_release_event = (xcb_key_press_event_t *)event;
				LOG_TRACE("Key %d released", key_release_event->detail);
			} break;
			case XCB_BUTTON_PRESS:
			case XCB_BUTTON_RELEASE: {
				LOG_INFO("Button pressed/released");
			} break;

			default: {
			} break;
		}
	}

	free(event);
}
bool x11_should_close(Platform *platform) {
	return platform->should_close;
}

uint64_t x11_time_ms(Platform *platform) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

void x11_get_logical_dimensions(Platform *platform, uint32_t *width, uint32_t *height) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	xcb_connection_t *connection = x11->connection;
	xcb_window_t *window = &x11->window;

	xcb_get_geometry_reply_t *geometry = x11->xcb.get_geometry_reply(connection, x11->xcb.get_geometry(connection, *window), NULL);
	*width = geometry->width;
	*height = geometry->height;
}

void x11_get_physical_dimensions(Platform *platform, uint32_t *width, uint32_t *height) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	xcb_get_geometry_reply_t *geometry = x11->xcb.get_geometry_reply(x11->connection, x11->xcb.get_geometry(x11->connection, x11->window), NULL);
	*width = geometry->width;
	*height = geometry->height;
}

void x11_set_logical_dimensions_callback(Platform *platform, fn_platform_dimensions callback_fn) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	x11->callback.logical_size = callback_fn;
}
void x11_set_physical_dimensions_callback(Platform *platform, fn_platform_dimensions callback_fn) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	x11->callback.physical_size = callback_fn;
}

bool x11_create_vulkan_surface(Platform *platform, VkInstance instance, VkSurfaceKHR *surface) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	VkXcbSurfaceCreateInfoKHR surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
		.window = x11->window,
		.connection = x11->connection,
	};

	if (vkCreateXcbSurfaceKHR(instance, &surface_create_info, NULL, surface) != VK_SUCCESS)
		return false;

	return true;
}

const char **x11_vulkan_extensions(Platform *platform, uint32_t *count) {
	*count = sizeof(extensions) / sizeof(*extensions);
	return extensions;
}
