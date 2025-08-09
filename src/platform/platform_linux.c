#include "core/logger.h"
#include "platform.h"

#include <xcb/xcb.h>

#include <stdlib.h>
#include <unistd.h>

struct platform {
	uint32_t width, height;
	bool should_close;

	xcb_connection_t *connection;
	xcb_window_t window;
};

Platform *platform_startup(Arena *arena, uint32_t width, uint32_t height, const char *title) {
	Platform *platform = arena_push_type(arena, struct platform);
	platform->width = width, platform->height = height;
	platform->should_close = false;

	platform->connection = xcb_connect(NULL, NULL);

	platform->window = xcb_generate_id(platform->connection);

	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(platform->connection)).data;

	xcb_intern_atom_cookie_t proto_cookie = xcb_intern_atom(platform->connection, 1, 12, "WM_PROTOCOLS");
	xcb_intern_atom_cookie_t close_cookie = xcb_intern_atom(platform->connection, 0, 16, "WM_DELETE_WINDOW");

	xcb_atom_t wm_protocols_atom = xcb_intern_atom_reply(platform->connection, proto_cookie, 0)->atom;
	xcb_atom_t wm_delete_window_atom = xcb_intern_atom_reply(platform->connection, close_cookie, 0)->atom;

	uint32_t event_mask =
		XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION |
		XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW;

	uint32_t values[1] = {
		event_mask
	};
	xcb_create_window(platform->connection, XCB_COPY_FROM_PARENT, platform->window, screen->root, 0, 0, platform->width, platform->height, 1, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_EVENT_MASK, values);

	xcb_map_window(platform->connection, platform->window);

	xcb_flush(platform->connection);

	xcb_change_property(platform->connection, XCB_PROP_MODE_REPLACE, platform->window, wm_protocols_atom, XCB_ATOM_ATOM, 32, 1, &wm_delete_window_atom);

	return platform;
}

void platform_shutdown(Platform *platform) {
	xcb_disconnect(platform->connection);
}

void platform_poll_events(Platform *platform) {
	xcb_generic_event_t *event = NULL;
	while ((event = xcb_poll_for_event(platform->connection))) {
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
bool platform_should_close(Platform *platform) {
	return platform->should_close;
}

void *platform_window_handle(Platform *platform) {
	return &platform->window;
}
void *platform_instance_handle(Platform *platform) {
	return platform->connection;
}
