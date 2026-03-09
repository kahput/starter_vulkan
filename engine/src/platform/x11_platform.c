#include "core/astring.h"
#include "core/debug.h"
#include "input/input_types.h"
#include "platform.h"

#include <string.h>
#include <xcb/xproto.h>

#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>

#include "common.h"
#include "core/logger.h"

#include <dlfcn.h>
#include <stdlib.h>

#include <linux/input-event-codes.h>

#if _POSIX_C_SOURCE >= 199309L
	#include <time.h>
#else
	#include <unistd.h>
#endif

void create_key_table(Window *window);
static const char *extensions[] = {
	"VK_KHR_surface",
	"VK_KHR_xcb_surface"
};

static struct {
	xcb_connection_t *connection;
	double start_time;
} x11 = { 0 };

struct window {
	xcb_window_t handle;
	xcb_screen_t *screen;
	xcb_cursor_t hidden_cursor;

	xcb_atom_t wm_delete_window_atom;
	xcb_atom_t wm_protocols_atom;
	xcb_atom_t wm_state_atom;
	xcb_atom_t wm_fullscreen_atom;

	uint32_t is_fullscreen, is_open, cursor_locked;
	int32_t keycodes[256];
	uint32_t width, height;
};

bool platform_startup(void) {
	x11.connection = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(x11.connection)) {
		LOG_ERROR("Failed to connect to X server");
		return false;
	}

	x11.start_time = platform_time();

	return true;
}

void platform_shutdown(void) {
	if (x11.connection)
		xcb_disconnect(x11.connection);
}

Window *window_make(Arena *arena, uint32_t width, uint32_t height, String title) {
	Window *window = arena_push_struct(arena, Window);

	window->handle = xcb_generate_id(x11.connection);

	create_key_table(window);

	const struct xcb_setup_t *setup = xcb_get_setup(x11.connection);
	window->screen = xcb_setup_roots_iterator(setup).data;

	xcb_intern_atom_reply_t *proto_reply = xcb_intern_atom_reply(x11.connection, xcb_intern_atom(x11.connection, 1, 12, "WM_PROTOCOLS"), 0);
	xcb_intern_atom_reply_t *close_reply = xcb_intern_atom_reply(x11.connection, xcb_intern_atom(x11.connection, 0, 16, "WM_DELETE_WINDOW"), 0);

	xcb_intern_atom_reply_t *state_reply = xcb_intern_atom_reply(x11.connection, xcb_intern_atom(x11.connection, 0, 13, "_NET_WM_STATE"), NULL);
	xcb_intern_atom_reply_t *fullscreen_reply = xcb_intern_atom_reply(x11.connection, xcb_intern_atom(x11.connection, 0, 24, "_NET_WM_STATE_FULLSCREEN"), NULL);

	if (!proto_reply || !close_reply || !state_reply || !fullscreen_reply) {
		LOG_ERROR("Failed to intern atoms");
		return NULL;
	}

	window->wm_protocols_atom = proto_reply->atom;
	window->wm_delete_window_atom = close_reply->atom;
	window->wm_state_atom = state_reply->atom;
	window->wm_fullscreen_atom = fullscreen_reply->atom;

	free(proto_reply);
	free(close_reply);
	free(state_reply);
	free(fullscreen_reply);

	uint32_t event_mask =
		XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION |
		XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		XCB_EVENT_MASK_PROPERTY_CHANGE;

	uint32_t value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	uint32_t values[] = {
		window->screen->black_pixel,
		event_mask
	};

	xcb_create_window(
		x11.connection,
		XCB_COPY_FROM_PARENT, // Depth
		window->handle,
		window->screen->root, // parent window
		0, 0, // x, y
		width, height, // width, height
		0, // border width
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		window->screen->root_visual,
		value_mask,
		values);
	xcb_map_window(x11.connection, window->handle);

	// Change title
	xcb_change_property(
		x11.connection,
		XCB_PROP_MODE_REPLACE,
		window->handle,
		XCB_ATOM_WM_NAME,
		XCB_ATOM_STRING,
		8,
		title.length,
		title.memory);

	// Register WM_DELETE_WINDOW
	xcb_change_property(
		x11.connection,
		XCB_PROP_MODE_REPLACE,
		window->handle,
		window->wm_protocols_atom,
		XCB_ATOM_ATOM,
		32,
		1,
		&window->wm_delete_window_atom);

	xcb_pixmap_t cursor_pixmap = xcb_generate_id(x11.connection);
	xcb_create_pixmap(
		x11.connection,
		1, // Depth
		cursor_pixmap,
		window->screen->root, // parent
		1, 1); // Width, height

	// Create the cursor using the blank pixmap for both source and mask
	// We use generic colors (0,0,0) - since the pixmap is empty, they won't render
	window->hidden_cursor = xcb_generate_id(x11.connection);
	xcb_create_cursor(
		x11.connection,
		window->hidden_cursor,
		cursor_pixmap,
		cursor_pixmap,
		0, 0, 0, // Foreground RGB
		0, 0, 0, // Background RGB
		0, 0 // Hotspot X, Y
	);

	// We can free the pixmap immediately after creating the cursor
	xcb_free_pixmap(x11.connection, cursor_pixmap);

	xcb_flush(x11.connection);
	window->is_open = true;
	return window;
}

void window_poll_events(Window *window) {
	xcb_generic_event_t *event = NULL;
	xcb_client_message_event_t *cm = NULL;

	while ((event = xcb_poll_for_event(x11.connection))) {
		switch (event->response_type & ~0x80) {
			case XCB_CLIENT_MESSAGE: {
				cm = (xcb_client_message_event_t *)event;
				if (cm->data.data32[0] == window->wm_delete_window_atom)
					window->is_open = false;
			} break;

			case XCB_CONFIGURE_NOTIFY: {
				xcb_configure_notify_event_t *cfg = (xcb_configure_notify_event_t *)event;

				if (cfg->width != window->width || cfg->height != window->height) {
					window->width = cfg->width;
					window->height = cfg->height;

					WindowResizeEvent e = {
						.width = cfg->width,
						.height = cfg->height,

					};
					event_emit_struct(EVENT_PLATFORM_WINDOW_RESIZED, &e);
				}
			} break;

			case XCB_PROPERTY_NOTIFY: {
				xcb_property_notify_event_t *pn = (xcb_property_notify_event_t *)event;
				if (pn->atom == window->wm_state_atom) {
					xcb_get_property_cookie_t cookie = xcb_get_property(
						x11.connection, 0, window->handle,
						window->wm_state_atom, XCB_ATOM_ATOM,
						0, 32);
					xcb_get_property_reply_t *reply = xcb_get_property_reply(x11.connection, cookie, NULL);

					window->is_fullscreen = false;
					if (reply) {
						xcb_atom_t *atoms = xcb_get_property_value(reply);
						uint32_t count = xcb_get_property_value_length(reply);

						for (uint32_t index = 0; index < count; ++index) {
							if (atoms[index] == window->wm_fullscreen_atom) {
								window->is_fullscreen = true;
								break;
							}
						}

						free(reply);
					}
				}
			} break;

			case XCB_MOTION_NOTIFY: {
				xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;

				MouseMotionEvent e = { 0 };

				/* if (window->pointer_mode == PLATFORM_POINTER_DISABLED) { */
				/* 	// Locked Mode Logic (Fake raw input) */
				/* 	int center_x = window->width / 2; */
				/* 	int center_y = window->height / 2; */

				/* 	// If we are at the center, this event was likely caused by our warp */
				/* 	if (motion->event_x == center_x && motion->event_y == center_y) { */
				/* 		break; */
				/* 	} */

				/* 	e.dx = (double)(motion->event_x - center_x); */
				/* 	e.dy = (double)(motion->event_y - center_y); */
				/* 	e.virtual_cursor = true; */

				/* 	// X11 doesn't have a built-in virtual accumulator, so we just emit deltas */
				/* 	// or you can track a virtual_x/y in your struct like Wayland does. */

				/* 	// Warp back to center */
				/* 	xcb_warp_pointer(x11.connection, XCB_NONE, window->handle, 0, 0, 0, 0, center_x, center_y); */
				/* 	xcb_flush(x11.connection); */
				/* } else { */

				e.x = (double)motion->event_x;
				e.y = (double)motion->event_y;

				/* } */

				event_emit_struct(EVENT_PLATFORM_MOUSE_MOTION, &e);
			} break;

			case XCB_BUTTON_PRESS:
			case XCB_BUTTON_RELEASE: {
				xcb_button_press_event_t *bp = (xcb_button_press_event_t *)event;
				bool pressed = (event->response_type & ~0x80) == XCB_BUTTON_PRESS;

				MouseButtonEvent e = { 0 };

				e.x = bp->event_x;
				e.y = bp->event_y;

				switch (bp->detail) {
					case 1:
						e.button = BTN_LEFT;
						break;
					case 2:
						e.button = BTN_MIDDLE;
						break;
					case 3:
						e.button = BTN_RIGHT;
						break;
					// X11 sends Scroll as buttons 4/5/6/7
					case 4: /* Handle Scroll Up if needed */
						break;
					case 5: /* Handle Scroll Down if needed */
						break;
					default:
						break;
						e.button = BTN_LEFT;
				}

				// If it's a standard button, emit event
				if (bp->detail <= 3) {
					event_emit_struct(pressed ? EVENT_PLATFORM_MOUSE_BUTTON_PRESSED : EVENT_PLATFORM_MOUSE_BUTTON_RELEASED, &e);
				}
			} break;

			case XCB_KEY_PRESS:
			case XCB_KEY_RELEASE: {
				xcb_key_press_event_t *kp = (xcb_key_press_event_t *)event;
				bool pressed = (event->response_type & ~0x80) == XCB_KEY_PRESS;

				KeyEvent e = { 0 };

				uint32_t scancode_index = kp->detail - 8;

				if (scancode_index < 256) {
					e.key = window->keycodes[scancode_index];
				} else {
					e.key = KEY_CODE_UNKOWN;
				}

				// You can add modifier logic here by checking kp->state
				// e.mods = ...

				event_emit_struct(pressed ? EVENT_PLATFORM_KEY_PRESSED : EVENT_PLATFORM_KEY_RELEASED, &e);
			} break;

			default:
				break;
		}
		free(event);
	}
}

bool window_is_open(Window *window) {
	return window->is_open;
}

void window_set_fullscreen(Window *window, bool fullscreen) {
	// Send a _NET_WM_STATE client message to the running WM.
	// This works on a mapped window. xcb_change_property would only work before mapping.
	xcb_client_message_event_t ev = {
		.response_type = XCB_CLIENT_MESSAGE,
		.format = 32,
		.window = window->handle,
		.type = window->wm_state_atom,
		.data.data32[0] = fullscreen ? 1 : 0, // 1 = add, 0 = remove
		.data.data32[1] = window->wm_fullscreen_atom,
	};

	xcb_send_event(
		x11.connection,
		0,
		window->screen->root,
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
		(const char *)&ev);
	window->is_fullscreen = fullscreen;

	xcb_flush(x11.connection);
}

bool window_is_fullscreen(Window *window) {
	return window->is_fullscreen;
}
void window_set_title(Window *window, String title) {
	xcb_change_property(
		x11.connection,
		XCB_PROP_MODE_REPLACE,
		window->handle,
		XCB_ATOM_WM_NAME,
		XCB_ATOM_STRING,
		8,
		title.length,
		title.memory);
	xcb_flush(x11.connection);
}

void window_set_cursor_visible(Window *window, bool visible) {
	if (visible) {
		uint32_t value = XCB_NONE;
		xcb_change_window_attributes(x11.connection, window->handle, XCB_CW_CURSOR, &value);
	} else
		xcb_change_window_attributes(x11.connection, window->handle, XCB_CW_CURSOR, &window->hidden_cursor);

	xcb_flush(x11.connection);
}

void window_set_cursor_locked(Window *window, bool locked) {
	if (locked == window->cursor_locked)
		return;

	window->cursor_locked = locked;

	if (locked) {
		xcb_grab_pointer_cookie_t cookie = xcb_grab_pointer(
			x11.connection,
			0, // owner_events: don't redirect events to grab window
			window->handle,
			XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
			XCB_GRAB_MODE_ASYNC,
			XCB_GRAB_MODE_ASYNC,
			window->handle, // confine to this window
			window->hidden_cursor, // change cursor - XCB_NONE for normal
			XCB_TIME_CURRENT_TIME);
		xcb_grab_pointer_reply_t *reply = xcb_grab_pointer_reply(x11.connection, cookie, NULL);

		xcb_warp_pointer(x11.connection, XCB_NONE, window->handle,
			0, 0, 0, 0, window->width * .5f, window->height * .5f);

		if (!reply || reply->status != XCB_GRAB_STATUS_SUCCESS)
			window->cursor_locked = false;

	} else
		xcb_ungrab_pointer(x11.connection, XCB_TIME_CURRENT_TIME);

	xcb_flush(x11.connection);
}

/* bool x11_pointer_mode(Platform *platform, PointerMode mode) { */
/* 	struct platform_internal *internal = (struct platform_internal *)platform->internal; */
/* 	X11Platform *x11 = &internal->x11; */

/* 	if (mode == x11->pointer_mode) */
/* 		return false; */

/* 	x11->pointer_mode = mode; */

/* 	if (mode == PLATFORM_POINTER_DISABLED) { */
/* 		// Warp to center first to avoid huge jump */
/* 		int center_x = platform->logical_width / 2; */
/* 		int center_y = platform->logical_height / 2; */

/* 		xcb_warp_pointer(x11->connection, XCB_NONE, x11->window, 0, 0, 0, 0, center_x, center_y); */

/* 		// Grab pointer */
/* 		// Note: x11->hidden_cursor must be initialized somewhere (e.g. x11_startup) */
/* 		// using xcb_create_cursor + xcb_create_pixmap for a blank cursor. */
/* 		xcb_grab_pointer( */
/* 			x11->connection, */
/* 			1, */
/* 			x11->window, */
/* 			XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION, */
/* 			XCB_GRAB_MODE_ASYNC, */
/* 			XCB_GRAB_MODE_ASYNC, */
/* 			x11->window, */
/* 			x11->hidden_cursor, */
/* 			XCB_TIME_CURRENT_TIME); */
/* 	} else { */
/* 		xcb_ungrab_pointer(x11->connection, XCB_TIME_CURRENT_TIME); */
/* 	} */

/* 	xcb_flush(x11->connection); */
/* 	return true; */
/* } */

double platform_time(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((double)ts.tv_sec + (double)ts.tv_nsec * 1e-9) - x11.start_time;
}

uint64_t platform_time_absolute(void) {
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (uint64_t)((time.tv_sec * 1000ULL * 1000ULL * 1000ULL) + time.tv_nsec);
}

void platform_sleep(uint32_t ms) {
#if _POSIX_C_SOURCE >= 199309L
	struct timespec time;
	time.tv_sec = ms * 1e-3;
	time.tv_nsec = (ms % 1000) * 1e6;
	nanosleep(&time, 0);
#else
	if (ms >= 1000)
		sleep(ms * 1e-3);
	usleep((ms % 1000) * 1e3);
#endif
}

uint32_2 window_size(Window *window) {
	xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(
		x11.connection,
		xcb_get_geometry(x11.connection, window->handle),
		NULL);

	if (geometry) {
		uint32_2 dims = {
			.x = geometry->width,
			.y = geometry->height
		};
		free(geometry);

		return dims;
	}

	ASSERT(false);
	return (uint32_2){ 0 };
}

uint32_2 window_size_pixel(Window *window) {
	return window_size(window);
}

void window_set_callback(PFN_event_handler handler) {
}

bool window_vulkan_surface_make(Window *window, struct VkInstance_T *instance, struct VkSurfaceKHR_T **out_surface) {
	VkXcbSurfaceCreateInfoKHR surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
		.window = window->handle,
		.connection = x11.connection,
	};

	if (vkCreateXcbSurfaceKHR(instance, &surface_create_info, NULL, out_surface) != VK_SUCCESS)
		return false;

	return true;
}

const char **platform_vulkan_extensions(uint32_t *count) {
	*count = sizeof(extensions) / sizeof(*extensions);
	return extensions;
}

void create_key_table(Window *window) {
	memset(window->keycodes, -1, sizeof(window->keycodes));

	window->keycodes[KEY_GRAVE] = KEY_CODE_GRAVE;
	window->keycodes[KEY_1] = KEY_CODE_1;
	window->keycodes[KEY_2] = KEY_CODE_2;
	window->keycodes[KEY_3] = KEY_CODE_3;
	window->keycodes[KEY_4] = KEY_CODE_4;
	window->keycodes[KEY_5] = KEY_CODE_5;
	window->keycodes[KEY_6] = KEY_CODE_6;
	window->keycodes[KEY_7] = KEY_CODE_7;
	window->keycodes[KEY_8] = KEY_CODE_8;
	window->keycodes[KEY_9] = KEY_CODE_9;
	window->keycodes[KEY_0] = KEY_CODE_0;
	window->keycodes[KEY_SPACE] = KEY_CODE_SPACE;
	window->keycodes[KEY_MINUS] = KEY_CODE_MINUS;
	window->keycodes[KEY_EQUAL] = KEY_CODE_EQUAL;
	window->keycodes[KEY_Q] = KEY_CODE_Q;
	window->keycodes[KEY_W] = KEY_CODE_W;
	window->keycodes[KEY_E] = KEY_CODE_E;
	window->keycodes[KEY_R] = KEY_CODE_R;
	window->keycodes[KEY_T] = KEY_CODE_T;
	window->keycodes[KEY_Y] = KEY_CODE_Y;
	window->keycodes[KEY_U] = KEY_CODE_U;
	window->keycodes[KEY_I] = KEY_CODE_I;
	window->keycodes[KEY_O] = KEY_CODE_O;
	window->keycodes[KEY_P] = KEY_CODE_P;
	window->keycodes[KEY_LEFTBRACE] = KEY_CODE_LEFTBRACKET;
	window->keycodes[KEY_RIGHTBRACE] = KEY_CODE_RIGHTBRACKET;
	window->keycodes[KEY_A] = KEY_CODE_A;
	window->keycodes[KEY_S] = KEY_CODE_S;
	window->keycodes[KEY_D] = KEY_CODE_D;
	window->keycodes[KEY_F] = KEY_CODE_F;
	window->keycodes[KEY_G] = KEY_CODE_G;
	window->keycodes[KEY_H] = KEY_CODE_H;
	window->keycodes[KEY_J] = KEY_CODE_J;
	window->keycodes[KEY_K] = KEY_CODE_K;
	window->keycodes[KEY_L] = KEY_CODE_L;
	window->keycodes[KEY_SEMICOLON] = KEY_CODE_SEMICOLON;
	window->keycodes[KEY_APOSTROPHE] = KEY_CODE_APOSTROPHE;
	window->keycodes[KEY_Z] = KEY_CODE_Z;
	window->keycodes[KEY_X] = KEY_CODE_X;
	window->keycodes[KEY_C] = KEY_CODE_C;
	window->keycodes[KEY_V] = KEY_CODE_V;
	window->keycodes[KEY_B] = KEY_CODE_B;
	window->keycodes[KEY_N] = KEY_CODE_N;
	window->keycodes[KEY_M] = KEY_CODE_M;
	window->keycodes[KEY_COMMA] = KEY_CODE_COMMA;
	window->keycodes[KEY_DOT] = KEY_CODE_PERIOD;
	window->keycodes[KEY_SLASH] = KEY_CODE_SLASH;
	window->keycodes[KEY_BACKSLASH] = KEY_CODE_BACKSLASH;
	window->keycodes[KEY_ESC] = KEY_CODE_ESCAPE;
	window->keycodes[KEY_TAB] = KEY_CODE_TAB;
	window->keycodes[KEY_LEFTSHIFT] = KEY_CODE_LEFTSHIFT;
	window->keycodes[KEY_RIGHTSHIFT] = KEY_CODE_RIGHTSHIFT;
	window->keycodes[KEY_LEFTCTRL] = KEY_CODE_LEFTCTRL;
	window->keycodes[KEY_RIGHTCTRL] = KEY_CODE_RIGHTCTRL;
	window->keycodes[KEY_LEFTALT] = KEY_CODE_LEFTALT;
	window->keycodes[KEY_RIGHTALT] = KEY_CODE_RIGHTALT;
	window->keycodes[KEY_LEFTMETA] = KEY_CODE_LEFTMETA;
	window->keycodes[KEY_RIGHTMETA] = KEY_CODE_RIGHTMETA;
	window->keycodes[KEY_COMPOSE] = KEY_CODE_MENU;
	window->keycodes[KEY_NUMLOCK] = KEY_CODE_NUMLOCK;
	window->keycodes[KEY_CAPSLOCK] = KEY_CODE_CAPSLOCK;
	window->keycodes[KEY_PRINT] = KEY_CODE_PRINT;
	window->keycodes[KEY_SCROLLLOCK] = KEY_CODE_SCROLLLOCK;
	window->keycodes[KEY_PAUSE] = KEY_CODE_PAUSE;
	window->keycodes[KEY_DELETE] = KEY_CODE_DELETE;
	window->keycodes[KEY_BACKSPACE] = KEY_CODE_BACKSPACE;
	window->keycodes[KEY_ENTER] = KEY_CODE_ENTER;
	window->keycodes[KEY_HOME] = KEY_CODE_HOME;
	window->keycodes[KEY_END] = KEY_CODE_END;
	window->keycodes[KEY_PAGEUP] = KEY_CODE_PAGEUP;
	window->keycodes[KEY_PAGEDOWN] = KEY_CODE_PAGEDOWN;
	window->keycodes[KEY_INSERT] = KEY_CODE_INSERT;
	window->keycodes[KEY_LEFT] = KEY_CODE_LEFT;
	window->keycodes[KEY_RIGHT] = KEY_CODE_RIGHT;
	window->keycodes[KEY_DOWN] = KEY_CODE_DOWN;
	window->keycodes[KEY_UP] = KEY_CODE_UP;
	window->keycodes[KEY_F1] = KEY_CODE_F1;
	window->keycodes[KEY_F2] = KEY_CODE_F2;
	window->keycodes[KEY_F3] = KEY_CODE_F3;
	window->keycodes[KEY_F4] = KEY_CODE_F4;
	window->keycodes[KEY_F5] = KEY_CODE_F5;
	window->keycodes[KEY_F6] = KEY_CODE_F6;
	window->keycodes[KEY_F7] = KEY_CODE_F7;
	window->keycodes[KEY_F8] = KEY_CODE_F8;
	window->keycodes[KEY_F9] = KEY_CODE_F9;
	window->keycodes[KEY_F10] = KEY_CODE_F10;
	window->keycodes[KEY_F11] = KEY_CODE_F11;
	window->keycodes[KEY_F12] = KEY_CODE_F12;
	window->keycodes[KEY_F13] = KEY_CODE_F13;
	window->keycodes[KEY_F14] = KEY_CODE_F14;
	window->keycodes[KEY_F15] = KEY_CODE_F15;
	window->keycodes[KEY_F16] = KEY_CODE_F16;
	window->keycodes[KEY_F17] = KEY_CODE_F17;
	window->keycodes[KEY_F18] = KEY_CODE_F18;
	window->keycodes[KEY_F19] = KEY_CODE_F19;
	window->keycodes[KEY_F20] = KEY_CODE_F20;
	window->keycodes[KEY_F21] = KEY_CODE_F21;
	window->keycodes[KEY_F22] = KEY_CODE_F22;
	window->keycodes[KEY_F23] = KEY_CODE_F23;
	window->keycodes[KEY_F24] = KEY_CODE_F24;
	window->keycodes[KEY_KPSLASH] = KEY_CODE_KPSLASH;
	window->keycodes[KEY_KPASTERISK] = KEY_CODE_KPASTERISK;
	window->keycodes[KEY_KPMINUS] = KEY_CODE_KPMINUS;
	window->keycodes[KEY_KPPLUS] = KEY_CODE_KPPLUS;
	window->keycodes[KEY_KP0] = KEY_CODE_KP0;
	window->keycodes[KEY_KP1] = KEY_CODE_KP1;
	window->keycodes[KEY_KP2] = KEY_CODE_KP2;
	window->keycodes[KEY_KP3] = KEY_CODE_KP3;
	window->keycodes[KEY_KP4] = KEY_CODE_KP4;
	window->keycodes[KEY_KP5] = KEY_CODE_KP5;
	window->keycodes[KEY_KP6] = KEY_CODE_KP6;
	window->keycodes[KEY_KP7] = KEY_CODE_KP7;
	window->keycodes[KEY_KP8] = KEY_CODE_KP8;
	window->keycodes[KEY_KP9] = KEY_CODE_KP9;
	window->keycodes[KEY_KPDOT] = KEY_CODE_KPDOT;
	window->keycodes[KEY_KPEQUAL] = KEY_CODE_KPEQUAL;
	window->keycodes[KEY_KPENTER] = KEY_CODE_KPENTER;
	window->keycodes[KEY_102ND] = KEY_CODE_WORLD_1;
}
