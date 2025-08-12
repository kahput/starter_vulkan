#include "platform/xcb_platform.h"
#include "platform/internal.h"

#include "core/logger.h"

#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <stdlib.h>

bool platform_init_x11(Platform *platform) {
	platform->internal->xcb.handle = dlopen("libxcb.so", RTLD_LAZY);
	if (platform->internal->xcb.handle == NULL)
		return false;

	void *handle = platform->internal->xcb.handle;

	// CONNECTION
	platform->internal->xcb.func.xcb_connect = (PFN_xcb_connect)dlsym(handle, "xcb_connect");
	platform->internal->xcb.func.xcb_disconnect = (PFN_xcb_disconnect)dlsym(handle, "xcb_disconnect");

	// SCREEN
	platform->internal->xcb.func.xcb_get_setup = (PFN_xcb_get_setup)dlsym(handle, "xcb_get_setup");
	platform->internal->xcb.func.xcb_setup_roots_iterator = (PFN_xcb_setup_roots_iterator)dlsym(handle, "xcb_setup_roots_iterator");

	// ATOM
	platform->internal->xcb.func.xcb_intern_atom = (PFN_xcb_intern_atom)dlsym(handle, "xcb_intern_atom");
	platform->internal->xcb.func.xcb_intern_atom_reply = (PFN_xcb_intern_atom_reply)dlsym(handle, "xcb_intern_atom_reply");

	// WINDOW
	platform->internal->xcb.func.xcb_generate_id = (PFN_xcb_generate_id)dlsym(handle, "xcb_generate_id");
	platform->internal->xcb.func.xcb_create_window = (PFN_xcb_create_window)dlsym(handle, "xcb_create_window");
	platform->internal->xcb.func.xcb_map_window = (PFN_xcb_map_window)dlsym(handle, "xcb_map_window");
	platform->internal->xcb.func.xcb_change_property = (PFN_xcb_change_property)dlsym(handle, "xcb_change_property");

	// GEMOTRY
	platform->internal->xcb.func.xcb_get_geometry = (PFN_xcb_get_geometry)dlsym(handle, "xcb_get_geometry");
	platform->internal->xcb.func.xcb_get_geometry_reply = (PFN_xcb_get_geometry_reply)dlsym(handle, "xcb_get_geometry_reply");

	// EVENT
	platform->internal->xcb.func.xcb_poll_for_event = (PFN_xcb_poll_for_event)dlsym(handle, "xcb_poll_for_event");
	platform->internal->xcb.func.xcb_flush = (PFN_xcb_flush)dlsym(handle, "xcb_flush");

	for (uint32_t i = 0; i < 10; i++) {
		if (platform->internal->xcb.func_array[i] == NULL) {
			return false;
		}
	}

	platform->internal->startup = xcb_platform_startup;
	platform->internal->shutdown = xcb_platform_shutdown;

	platform->internal->poll_events = xcb_platform_poll_events;
	platform->internal->should_close = xcb_platform_should_close;

	platform->internal->window_size = xcb_platform_get_window_size;
	platform->internal->framebuffer_size = xcb_platform_get_framebuffer_size;

	platform->internal->create_vulkan_surface = xcb_platform_create_vulkan_surface;

	return true;
}

bool xcb_platform_startup(Platform *platform) {
	platform->should_close = false;

	platform->internal->xcb.connection = platform->internal->xcb.func.xcb_connect(NULL, NULL);
	xcb_connection_t *connection = platform->internal->xcb.connection;

	platform->internal->xcb.window = platform->internal->xcb.func.xcb_generate_id(connection);
	xcb_window_t window = platform->internal->xcb.window;

	xcb_screen_t *screen = platform->internal->xcb.func.xcb_setup_roots_iterator(platform->internal->xcb.func.xcb_get_setup(connection)).data;

	xcb_intern_atom_cookie_t proto_cookie = platform->internal->xcb.func.xcb_intern_atom(connection, 1, 12, "WM_PROTOCOLS");
	xcb_intern_atom_cookie_t close_cookie = platform->internal->xcb.func.xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW");

	xcb_atom_t wm_protocols_atom = platform->internal->xcb.func.xcb_intern_atom_reply(connection, proto_cookie, 0)->atom;
	xcb_atom_t wm_delete_window_atom = platform->internal->xcb.func.xcb_intern_atom_reply(connection, close_cookie, 0)->atom;

	uint32_t event_mask =
		XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION |
		XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW;

	uint32_t values[1] = {
		event_mask
	};
	platform->internal->xcb.func.xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, platform->width, platform->height, 1, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_EVENT_MASK, values);

	platform->internal->xcb.func.xcb_map_window(connection, window);

	platform->internal->xcb.func.xcb_flush(connection);

	platform->internal->xcb.func.xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, wm_protocols_atom, XCB_ATOM_ATOM, 32, 1, &wm_delete_window_atom);

	return true;
}

void xcb_platform_shutdown(Platform *platform) {
	platform->internal->xcb.func.xcb_disconnect(platform->internal->xcb.connection);
}

void xcb_platform_poll_events(Platform *platform) {
	xcb_connection_t *connection = platform->internal->xcb.connection;
	xcb_generic_event_t *event = NULL;
	while ((event = platform->internal->xcb.func.xcb_poll_for_event(connection))) {
		switch (event->response_type & ~0x80) {
			case XCB_CLIENT_MESSAGE: {
				platform->should_close = true;
			} break;

			case XCB_KEY_PRESS:
			case XCB_KEY_RELEASE: {
				LOG_INFO("Key pressed/released");
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
bool xcb_platform_should_close(Platform *platform) {
	return platform->should_close;
}

void xcb_platform_get_window_size(Platform *platform, uint32_t *width, uint32_t *height) {
	xcb_connection_t *connection = platform->internal->xcb.connection;
	xcb_window_t *window = &platform->internal->xcb.window;

	xcb_get_geometry_reply_t *geometry = platform->internal->xcb.func.xcb_get_geometry_reply(connection, platform->internal->xcb.func.xcb_get_geometry(connection, *window), NULL);
	*width = geometry->width;
	*height = geometry->height;
}

void xcb_platform_get_framebuffer_size(Platform *platform, uint32_t *width, uint32_t *height) {
	xcb_connection_t *connection = platform->internal->xcb.connection;
	xcb_window_t *window = &platform->internal->xcb.window;

	xcb_get_geometry_reply_t *geometry = platform->internal->xcb.func.xcb_get_geometry_reply(connection, platform->internal->xcb.func.xcb_get_geometry(connection, *window), NULL);
	*width = geometry->width;
	*height = geometry->height;
}

bool xcb_platform_create_vulkan_surface(Platform *platform, VkInstance instance, VkSurfaceKHR *surface) {
	VkXcbSurfaceCreateInfoKHR surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
		.window = platform->internal->xcb.window,
		.connection = platform->internal->xcb.connection,
	};

	if (vkCreateXcbSurfaceKHR(instance, &surface_create_info, NULL, surface) != VK_SUCCESS)
		return false;

	return true;
}
