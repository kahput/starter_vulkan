#include "platform/xcb_platform.h"
#include "platform/internal.h"

#include "core/logger.h"

#include <dlfcn.h>
#include <stdlib.h>

bool platform_init_x11(Platform *platform) {
	void *handle = dlopen("libxcb.so", RTLD_LAZY);
	if (handle)
		return true;
	return false;
}

bool xcb_platform_startup(Platform *platform) {
	// platform->should_close = false;
	//
	// xcb_connection_t *connection = platform->internal->xcb.connection;
	// xcb_window_t *window = &platform->internal->xcb.window;
	//
	// connection = platform->internal->xcb.API.xcb_connect(NULL, NULL);
	//
	// platform->internal->xcb.window = xcb_generate_id(connection);
	//
	// xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
	//
	// xcb_intern_atom_cookie_t proto_cookie = xcb_intern_atom(connection, 1, 12, "WM_PROTOCOLS");
	// xcb_intern_atom_cookie_t close_cookie = xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW");
	//
	// xcb_atom_t wm_protocols_atom = xcb_intern_atom_reply(connection, proto_cookie, 0)->atom;
	// xcb_atom_t wm_delete_window_atom = xcb_intern_atom_reply(connection, close_cookie, 0)->atom;
	//
	// uint32_t event_mask =
	// 	XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
	// 	XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
	// 	XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION |
	// 	XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW;
	//
	// uint32_t values[1] = {
	// 	event_mask
	// };
	// xcb_create_window(connection, XCB_COPY_FROM_PARENT, *window, screen->root, 0, 0, platform->width, platform->height, 1, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_EVENT_MASK, values);
	//
	// xcb_map_window(connection, *window);
	//
	// xcb_flush(connection);
	//
	// xcb_change_property(connection, XCB_PROP_MODE_REPLACE, *window, wm_protocols_atom, XCB_ATOM_ATOM, 32, 1, &wm_delete_window_atom);
	//
	// return platform;
	// re
	return NULL;
}

void xcb_platform_shutdown(Platform *platform) {
	// xcb_connection_t *connection = platform->internal->xcb.connection;
	// xcb_disconnect(connection);
}

void xcb_platform_poll_events(Platform *platform) {
	// xcb_connection_t *connection = platform->internal->xcb.connection;
	// xcb_generic_event_t *event = NULL;
	// while ((event = xcb_poll_for_event(connection))) {
	// 	switch (event->response_type & ~0x80) {
	// 		case XCB_CLIENT_MESSAGE: {
	// 			platform->should_close = true;
	// 		} break;
	//
	// 		case XCB_KEY_PRESS:
	// 		case XCB_KEY_RELEASE: {
	// 			LOG_INFO("Key pressed/released");
	// 		} break;
	// 		case XCB_BUTTON_PRESS:
	// 		case XCB_BUTTON_RELEASE: {
	// 			LOG_INFO("Button pressed/released");
	// 		} break;
	//
	// 		default: {
	// 		} break;
	// 	}
	// }
	//
	// free(event);
}
bool xcb_platform_should_close(Platform *platform) {
	/* return platform->should_close; */
	return false;
}

void *xcb_platform_window_handle(Platform *platform) {
	// xcb_window_t *window = &platform->internal->xcb.window;
	// return window;
	return NULL;
}
void *xcb_platform_instance_handle(Platform *platform) {
	// xcb_connection_t *connection = platform->internal->xcb.connection;
	// return connection;
	return NULL;
}
