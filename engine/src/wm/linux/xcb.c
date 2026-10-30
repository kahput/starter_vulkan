#include "wm.h"
#include "common.h"
#include "core/debug.h"
#include "core/input_types.h"
#include "core/logger.h"

#include <stdlib.h>
#include <xcb/xcb.h>
/* #include <xcb/xcb_icccm.h> // sizing hints */
#include <linux/input-event-codes.h>

// EXTENSIONS
#include <xcb/xkb.h> // disable virtual release
#include <xcb/xproto.h>

struct WM_Surface {
	uint32_t handle;
	uint32_t width, height;

	int2 virtual_cursor;
	bool cursor_captured, warping, minimized, occluded;
};

static uint8_t keycode_mapping_table[KEY_CODE_MAX] = {
	[KEY_GRAVE] = KEY_CODE_GRAVE,
	[KEY_1] = KEY_CODE_1,
	[KEY_2] = KEY_CODE_2,
	[KEY_3] = KEY_CODE_3,
	[KEY_4] = KEY_CODE_4,
	[KEY_5] = KEY_CODE_5,
	[KEY_6] = KEY_CODE_6,
	[KEY_7] = KEY_CODE_7,
	[KEY_8] = KEY_CODE_8,
	[KEY_9] = KEY_CODE_9,
	[KEY_0] = KEY_CODE_0,
	[KEY_SPACE] = KEY_CODE_SPACE,
	[KEY_MINUS] = KEY_CODE_MINUS,
	[KEY_EQUAL] = KEY_CODE_EQUAL,
	[KEY_Q] = KEY_CODE_Q,
	[KEY_W] = KEY_CODE_W,
	[KEY_E] = KEY_CODE_E,
	[KEY_R] = KEY_CODE_R,
	[KEY_T] = KEY_CODE_T,
	[KEY_Y] = KEY_CODE_Y,
	[KEY_U] = KEY_CODE_U,
	[KEY_I] = KEY_CODE_I,
	[KEY_O] = KEY_CODE_O,
	[KEY_P] = KEY_CODE_P,
	[KEY_LEFTBRACE] = KEY_CODE_LEFTBRACKET,
	[KEY_RIGHTBRACE] = KEY_CODE_RIGHTBRACKET,
	[KEY_A] = KEY_CODE_A,
	[KEY_S] = KEY_CODE_S,
	[KEY_D] = KEY_CODE_D,
	[KEY_F] = KEY_CODE_F,
	[KEY_G] = KEY_CODE_G,
	[KEY_H] = KEY_CODE_H,
	[KEY_J] = KEY_CODE_J,
	[KEY_K] = KEY_CODE_K,
	[KEY_L] = KEY_CODE_L,
	[KEY_SEMICOLON] = KEY_CODE_SEMICOLON,
	[KEY_APOSTROPHE] = KEY_CODE_APOSTROPHE,
	[KEY_Z] = KEY_CODE_Z,
	[KEY_X] = KEY_CODE_X,
	[KEY_C] = KEY_CODE_C,
	[KEY_V] = KEY_CODE_V,
	[KEY_B] = KEY_CODE_B,
	[KEY_N] = KEY_CODE_N,
	[KEY_M] = KEY_CODE_M,
	[KEY_COMMA] = KEY_CODE_COMMA,
	[KEY_DOT] = KEY_CODE_PERIOD,
	[KEY_SLASH] = KEY_CODE_SLASH,
	[KEY_BACKSLASH] = KEY_CODE_BACKSLASH,
	[KEY_ESC] = KEY_CODE_ESCAPE,
	[KEY_TAB] = KEY_CODE_TAB,
	[KEY_LEFTSHIFT] = KEY_CODE_LEFTSHIFT,
	[KEY_RIGHTSHIFT] = KEY_CODE_RIGHTSHIFT,
	[KEY_LEFTCTRL] = KEY_CODE_LEFTCTRL,
	[KEY_RIGHTCTRL] = KEY_CODE_RIGHTCTRL,
	[KEY_LEFTALT] = KEY_CODE_LEFTALT,
	[KEY_RIGHTALT] = KEY_CODE_RIGHTALT,
	[KEY_LEFTMETA] = KEY_CODE_LEFTMETA,
	[KEY_RIGHTMETA] = KEY_CODE_RIGHTMETA,
	[KEY_COMPOSE] = KEY_CODE_MENU,
	[KEY_NUMLOCK] = KEY_CODE_NUMLOCK,
	[KEY_CAPSLOCK] = KEY_CODE_CAPSLOCK,
	[KEY_PRINT] = KEY_CODE_PRINT,
	[KEY_SCROLLLOCK] = KEY_CODE_SCROLLLOCK,
	[KEY_PAUSE] = KEY_CODE_PAUSE,
	[KEY_DELETE] = KEY_CODE_DELETE,
	[KEY_BACKSPACE] = KEY_CODE_BACKSPACE,
	[KEY_ENTER] = KEY_CODE_ENTER,
	[KEY_HOME] = KEY_CODE_HOME,
	[KEY_END] = KEY_CODE_END,
	[KEY_PAGEUP] = KEY_CODE_PAGEUP,
	[KEY_PAGEDOWN] = KEY_CODE_PAGEDOWN,
	[KEY_INSERT] = KEY_CODE_INSERT,
	[KEY_LEFT] = KEY_CODE_LEFT,
	[KEY_RIGHT] = KEY_CODE_RIGHT,
	[KEY_DOWN] = KEY_CODE_DOWN,
	[KEY_UP] = KEY_CODE_UP,
	[KEY_F1] = KEY_CODE_F1,
	[KEY_F2] = KEY_CODE_F2,
	[KEY_F3] = KEY_CODE_F3,
	[KEY_F4] = KEY_CODE_F4,
	[KEY_F5] = KEY_CODE_F5,
	[KEY_F6] = KEY_CODE_F6,
	[KEY_F7] = KEY_CODE_F7,
	[KEY_F8] = KEY_CODE_F8,
	[KEY_F9] = KEY_CODE_F9,
	[KEY_F10] = KEY_CODE_F10,
	[KEY_F11] = KEY_CODE_F11,
	[KEY_F12] = KEY_CODE_F12,
	[KEY_F13] = KEY_CODE_F13,
	[KEY_F14] = KEY_CODE_F14,
	[KEY_F15] = KEY_CODE_F15,
	[KEY_F16] = KEY_CODE_F16,
	[KEY_F17] = KEY_CODE_F17,
	[KEY_F18] = KEY_CODE_F18,
	[KEY_F19] = KEY_CODE_F19,
	[KEY_F20] = KEY_CODE_F20,
	[KEY_F21] = KEY_CODE_F21,
	[KEY_F22] = KEY_CODE_F22,
	[KEY_F23] = KEY_CODE_F23,
	[KEY_F24] = KEY_CODE_F24,
	[KEY_KPSLASH] = KEY_CODE_KPSLASH,
	[KEY_KPASTERISK] = KEY_CODE_KPASTERISK,
	[KEY_KPMINUS] = KEY_CODE_KPMINUS,
	[KEY_KPPLUS] = KEY_CODE_KPPLUS,
	[KEY_KP0] = KEY_CODE_KP0,
	[KEY_KP1] = KEY_CODE_KP1,
	[KEY_KP2] = KEY_CODE_KP2,
	[KEY_KP3] = KEY_CODE_KP3,
	[KEY_KP4] = KEY_CODE_KP4,
	[KEY_KP5] = KEY_CODE_KP5,
	[KEY_KP6] = KEY_CODE_KP6,
	[KEY_KP7] = KEY_CODE_KP7,
	[KEY_KP8] = KEY_CODE_KP8,
	[KEY_KP9] = KEY_CODE_KP9,
	[KEY_KPDOT] = KEY_CODE_KPDOT,
	[KEY_KPEQUAL] = KEY_CODE_KPEQUAL,
	[KEY_KPENTER] = KEY_CODE_KPENTER,
	[KEY_102ND] = KEY_CODE_WORLD_1,
};

typedef struct {
	xcb_connection_t *conn;
	WM_Surface root;

	xcb_cursor_t hidden_cursor;

	xcb_atom_t wm_protocols_atom;
	xcb_atom_t wm_delete_window_atom;
	xcb_atom_t wm_transient_for_atom;
	xcb_atom_t wm_window_type_atom;
	xcb_atom_t wm_state_atom;

	uint64_t start_time;

	WM_Surface surfaces[32];
	uint32_t surface_count;

	WM_Surface *fullscreen;

	bool initialized;
} WM_State;

static WM_State wm_state[1];

static inline xcb_intern_atom_reply_t *wm__atom(string8 name);
static inline void wm__surface_set_min_max(WM_Surface *surface, uint32_t min_w, uint32_t min_h, uint32_t max_w, uint32_t max_h);
static inline WM_Surface *wm__surface_from_handle(xcb_window_t window);

typedef enum {
	ATOM_PROTOCOL,
	ATOM_CLOSE,
	ATOM_TRANSIENT,
	ATOM_WINDOW_STATE,
	ATOM_WINDOW_TYPE,
} Atom;

bool wm_startup(void) {
	bool ok = true;
	if (ok) {
		wm_state->conn = xcb_connect(0, 0);
		ok = xcb_connection_has_error(wm_state->conn) == 0;
		if (ok == false)
			LOG_ERROR("%s - connection request to X server failed.", __func__);
	}

	xcb_intern_atom_reply_t *proto_reply = 0;
	xcb_intern_atom_reply_t *close_reply = 0;
	xcb_intern_atom_reply_t *transient_reply = 0;
	xcb_intern_atom_reply_t *type_reply = 0;
	xcb_intern_atom_reply_t *state_reply = 0;
	if (ok) {
		wm_state->root.handle = xcb_setup_roots_iterator(xcb_get_setup(wm_state->conn)).data->root;
		xcb_xkb_use_extension(wm_state->conn, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);
		xcb_xkb_per_client_flags(wm_state->conn, XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 1, 0, 0, 0);

		xcb_pixmap_t cursor_pixmap = xcb_generate_id(wm_state->conn);
		xcb_create_pixmap(
			wm_state->conn,
			1, // Depth
			cursor_pixmap,
			wm_state->root.handle,
			1, 1); // Width, height
		wm_state->hidden_cursor = xcb_generate_id(wm_state->conn);
		xcb_create_cursor(
			wm_state->conn,
			wm_state->hidden_cursor,
			cursor_pixmap,
			cursor_pixmap,
			0, 0, 0, // Foreground RGB
			0, 0, 0, // Background RGB
			0, 0 // Hotspot X, Y
		);
		xcb_free_pixmap(wm_state->conn, cursor_pixmap);

		const char *atom_strings[] = {
			"WM_PROTOCOLS",
			"WM_DELETE_WINDOW"
			"WM_TRANSIENT_FOR"
			"_NET_WM_WINDOW_TYPE"
		};

		proto_reply = wm__atom(s("WM_PROTOCOLS"));
		close_reply = wm__atom(s("WM_DELETE_WINDOW"));
		transient_reply = wm__atom(s("WM_TRANSIENT_FOR"));
		type_reply = wm__atom(s("_NET_WM_WINDOW_TYPE"));
		state_reply = wm__atom(s("_NET_WM_STATE"));

		ok = proto_reply && close_reply && transient_reply && type_reply && state_reply;
		if (ok == false)
			LOG_INFO("failed to aquire X server protocol.");
	}

	if (ok) {
		wm_state->wm_protocols_atom = proto_reply->atom;
		wm_state->wm_delete_window_atom = close_reply->atom;
		wm_state->wm_transient_for_atom = transient_reply->atom;
		wm_state->wm_window_type_atom = type_reply->atom;
		wm_state->wm_state_atom = state_reply->atom;
	}

	if (close_reply)
		free(close_reply);
	if (proto_reply)
		free(proto_reply);
	if (transient_reply)
		free(transient_reply);
	if (type_reply)
		free(type_reply);
	if (state_reply)
		free(state_reply);

	wm_state->initialized = true;
	return true;
}

void wm_shutdown(void) {
	for (uint32_t index = 0; index < wm_state->surface_count; ++index)
		wm_surface_close(wm_state->surfaces + index);

	if (wm_state->conn)
		xcb_disconnect(wm_state->conn);
}

WM_Surface *wm_surface_open(uint32_t width, uint32_t height, string8 title, WM_SurfaceFlags flags) {
	return wm_surface_open_with_parent(&wm_state->root, width, height, title, flags);
}

WM_Surface *wm_surface_open_with_parent(WM_Surface *parent, uint32_t width, uint32_t height, string8 title, WM_SurfaceFlags flags) {
	bool ok = wm_state->initialized;
	if (ok == false)
		LOG_WARN("%s - X server uninitialized, call wm_startup first.", __func__);

	WM_Surface *surface = 0;
	ok = parent && wm_state->surface_count < countof(wm_state->surfaces);

	if (ok) {
		surface = &wm_state->surfaces[wm_state->surface_count++];

		uint32_t event_mask =
			XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
			XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
			XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION |
			XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
			XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE |
			XCB_EVENT_MASK_VISIBILITY_CHANGE;

		uint32_t value_mask = XCB_CW_EVENT_MASK; // XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
		uint32_t values[] = {
			/* 0x00aade87, */
			event_mask
		};

		surface->width = width;
		surface->height = height;

		surface->handle = xcb_generate_id(wm_state->conn);

		xcb_create_window(
			wm_state->conn,
			XCB_COPY_FROM_PARENT, // Depth
			surface->handle,
			wm_state->root.handle, // parent window
			0, 0, // x, y
			surface->width, surface->height, // width, height
			0, // border width
			XCB_WINDOW_CLASS_INPUT_OUTPUT,
			XCB_COPY_FROM_PARENT,
			value_mask,
			values);
		if (has_flag(flags, WM_SURFACE_FLAG_RESIZEABLE) == false)
			wm__surface_set_min_max(surface, surface->width, surface->height, width, height);

		// Change title
		xcb_change_property(
			wm_state->conn,
			XCB_PROP_MODE_REPLACE,
			surface->handle,
			XCB_ATOM_WM_NAME,
			XCB_ATOM_STRING,
			8,
			title.length,
			title.bytes);
		// Register WM_DELETE_WINDOW
		xcb_change_property(
			wm_state->conn,
			XCB_PROP_MODE_REPLACE,
			surface->handle,
			wm_state->wm_delete_window_atom,
			XCB_ATOM_ATOM,
			32,
			1,
			&wm_state->wm_delete_window_atom);

		xcb_map_window(wm_state->conn, surface->handle);
		xcb_flush(wm_state->conn);
	}

	return surface;
}

void wm_surface_close(WM_Surface *surface) {
	xcb_destroy_window(wm_state->conn, surface->handle);
	xcb_flush(wm_state->conn);
}

bool wm_surface_valid(WM_Surface *surface) {
	return wm_state->initialized && surface && surface > wm_state->surfaces && surface < wm_state->surfaces + countof(wm_state->surfaces);
}

bool wm_surface_minimized(WM_Surface *surface) {
	bool ok = wm_state->initialized && surface && wm_surface_valid(surface);
	if (ok)
		ok = surface->minimized;
	return ok;
}

bool wm_surface_drawable(WM_Surface *surface) {
	return wm_surface_valid(surface) && surface->minimized == false && surface->occluded == false;
}

void wm_surface_show(WM_Surface *surface) {
	bool ok = wm_state->initialized && surface && wm_surface_valid(surface);
	if (ok) {
		xcb_map_window(wm_state->conn, surface->handle);
		xcb_flush(wm_state->conn);
	}
}
void wm_surface_hide(WM_Surface *surface) {
	bool ok = wm_state->initialized && surface && wm_surface_valid(surface);
	if (ok) {
		xcb_unmap_window(wm_state->conn, surface->handle);
		xcb_flush(wm_state->conn);
	}
}

bool wm_poll(WM_Event *out) {
	xcb_generic_event_t *src = xcb_poll_for_event(wm_state->conn);
	if (src == 0 || out == 0) return false;
	memory_zero(out, sizeof(WM_Event));

	uint32_t event_type = src->response_type & ~0x80;
	switch (event_type) {
		case XCB_CLIENT_MESSAGE: {
			xcb_client_message_event_t *cm = (xcb_client_message_event_t *)src;
			if (cm->data.data32[0] == wm_state->wm_delete_window_atom) {
				out->surface = wm__surface_from_handle(cm->window);
				ASSERT_MESSAGE(out->surface != 0, "The event doesn't belong to any window");
				out->type = WM_EVENT_TYPE_SURFACE_CLOSE;
			}
		} break;

		case XCB_CONFIGURE_NOTIFY: {
			xcb_configure_notify_event_t *cfg = (xcb_configure_notify_event_t *)src;

			WM_Surface *surface = wm__surface_from_handle(cfg->window);
			if (surface && (surface->width != cfg->width || surface->height != cfg->height)) {
				out->type = WM_EVENT_TYPE_SURFACE_RESIZE;
				out->surface = surface;

				surface->width = cfg->width;
				surface->height = cfg->height;

				out->as.resize.width = surface->width;
				out->as.resize.height = surface->height;
			}
		} break;
		case XCB_VISIBILITY_NOTIFY: {
			xcb_visibility_notify_event_t *ev = (xcb_visibility_notify_event_t *)src;
			WM_Surface *surface = wm__surface_from_handle(ev->window);
			if (surface)
				surface->occluded = ev->state == XCB_VISIBILITY_FULLY_OBSCURED;
		} break;

		case XCB_UNMAP_NOTIFY:
		case XCB_MAP_NOTIFY: {
			xcb_map_notify_event_t *mn = (xcb_map_notify_event_t *)src;
			WM_Surface *surface = wm__surface_from_handle(mn->window);
			if (surface)
				surface->minimized = event_type != XCB_MAP_NOTIFY;
		} break;

		case XCB_KEY_PRESS:
		case XCB_KEY_RELEASE: {
			xcb_key_press_event_t *kp = (xcb_key_press_event_t *)src;

			uint32_t scancode = kp->detail - 8;
			if (scancode < 256) {
				bool released = (src->response_type & ~0x80) == XCB_KEY_RELEASE;
				KeyCode key = keycode_mapping_table[scancode];

				out->type = WM_EVENT_TYPE_KEY_PRESS + released;
				out->as.key.key_code = key;
			}
		} break;
		case XCB_BUTTON_PRESS:
		case XCB_BUTTON_RELEASE: {
			xcb_button_press_event_t *bp = (xcb_button_press_event_t *)src;

			if (bp->detail <= 3) {
				bool released = (src->response_type & ~0x80) == XCB_BUTTON_RELEASE;
				out->surface = wm__surface_from_handle(bp->event);
				out->type = WM_EVENT_TYPE_MOUSE_PRESS + released;
				out->as.mouse_button.x = bp->event_x;
				out->as.mouse_button.y = bp->event_y;
				out->as.mouse_button.button =
					bp->detail == 1
					? MOUSE_BUTTON_LEFT
					: bp->detail == 2
					? MOUSE_BUTTON_MIDDLE
					: MOUSE_BUTTON_RIGHT;
			}
		} break;
		case XCB_MOTION_NOTIFY: {
			xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)src;
			WM_Surface *surface = wm__surface_from_handle(motion->event);

			out->type = WM_EVENT_TYPE_MOUSE_MOTION;
			out->surface = surface;

			int2 delta;
			if (surface->cursor_captured) {
				if (surface->warping) {
					surface->warping = false;
					break;
				}

				int2 center = { surface->width / 2, surface->height / 2 };
				delta.x = (int32_t)motion->event_x - center.x;
				delta.y = (int32_t)motion->event_y - center.y;

				surface->virtual_cursor.x += delta.x;
				surface->virtual_cursor.y += delta.y;

				if (delta.x != 0 || delta.y != 0) {
					surface->warping = true;
					xcb_warp_pointer(wm_state->conn, XCB_NONE, surface->handle,
						0, 0, 0, 0, center.x, center.y);
					xcb_flush(wm_state->conn);
				}

				motion->event_x = surface->virtual_cursor.x;
				motion->event_y = surface->virtual_cursor.y;
			} else {
				delta = (int2){ motion->event_x - surface->virtual_cursor.x, motion->event_y - surface->virtual_cursor.y };
				surface->virtual_cursor.x = motion->event_x;
				surface->virtual_cursor.y = motion->event_y;
			}

			out->as.mouse_motion.x = surface->virtual_cursor.y;
			out->as.mouse_motion.y = surface->virtual_cursor.x;
			out->as.mouse_motion.dx = delta.x;
			out->as.mouse_motion.dy = delta.y;

		} break;
	}
	free(src);

	return true;
}

float wm_surface_dpi(WM_Surface *surface) {
	return 1.0f;
}
uint2 wm_surface_size(WM_Surface *surface) {
	return (uint2){ surface->width, surface->height };
}

/* void wm_cursor_show(bool show); */
/* void wm_cursor_capture(WM_Surface *surface, bool capture); */
/* void wm_cursor_set_position(WM_Surface *surface, int32_t x, int32_t y); */

static const char *extensions[] = {
	"VK_KHR_surface",
	"VK_KHR_xcb_surface",
};
const char **wm_vulkan_surface_extensions(uint32_t *count) {
	*count = countof(extensions);
	return extensions;
}

void *wm_native_display(void) {
	return (void *)wm_state->conn;
}
void *wm_native_surface(WM_Surface *surface) {
	return (void *)(uint64_t)surface->handle;
}

void wm_cursor_show(WM_Surface *surface, bool show) {
	if (show) {
		uint32_t value = XCB_NONE;
		xcb_change_window_attributes(wm_state->conn, surface->handle, XCB_CW_CURSOR, &value);
	} else
		xcb_change_window_attributes(wm_state->conn, surface->handle, XCB_CW_CURSOR, &wm_state->hidden_cursor);

	xcb_flush(wm_state->conn);
}

void wm_cursor_capture(WM_Surface *surface, bool capture) {
	if (capture == surface->cursor_captured) return;
	surface->cursor_captured = capture;

	if (capture) {
		xcb_grab_pointer_cookie_t cookie = xcb_grab_pointer(
			wm_state->conn,
			0, // owner_events: don't redirect events to grab window
			surface->handle,
			XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
			XCB_GRAB_MODE_ASYNC,
			XCB_GRAB_MODE_ASYNC,
			surface->handle, // confine to this window
			wm_state->hidden_cursor, // change cursor - XCB_NONE for normal
			XCB_TIME_CURRENT_TIME);
		xcb_grab_pointer_reply_t *reply = xcb_grab_pointer_reply(wm_state->conn, cookie, NULL);

		surface->virtual_cursor.x = surface->width / 2;
		surface->virtual_cursor.y = surface->height / 2;

		xcb_warp_pointer(wm_state->conn, XCB_NONE, surface->handle,
			0, 0, 0, 0, surface->virtual_cursor.x, surface->virtual_cursor.y);
		surface->warping = true; // flag to swallow the warp event

		if (reply->status != XCB_GRAB_STATUS_SUCCESS) surface->cursor_captured = false;
		free(reply);
	} else
		xcb_ungrab_pointer(wm_state->conn, XCB_TIME_CURRENT_TIME);

	xcb_flush(wm_state->conn);
}
bool wm_cursor_captured(WM_Surface *surface) {
	return surface->cursor_captured;
}

void wm_cursor_set_position(WM_Surface *surface, int32_t x, int32_t y) {
	xcb_warp_pointer(wm_state->conn, XCB_NONE, surface->handle, 0, 0, 0, 0, x, y);
	surface->warping = true;
	xcb_flush(wm_state->conn);
}

static inline xcb_intern_atom_reply_t *wm__atom(string8 name) {
	xcb_intern_atom_reply_t *result = xcb_intern_atom_reply(wm_state->conn, xcb_intern_atom(wm_state->conn, 1, name.length, (char *)name.bytes), 0);
	return result;
}

void wm__surface_set_min_max(WM_Surface *surface, uint32_t min_w, uint32_t min_h, uint32_t max_w, uint32_t max_h) {
	// source - https://stackoverflow.com/a/59762666
	typedef struct {
		/** User specified flags */
		uint32_t flags;
		/** User-specified position */
		int32_t x, y;
		/** User-specified size */
		int32_t width, height;
		/** Program-specified minimum size */
		int32_t min_width, min_height;
		/** Program-specified maximum size */
		int32_t max_width, max_height;
		/** Program-specified resize increments */
		int32_t width_inc, height_inc;
		/** Program-specified minimum aspect ratios */
		int32_t min_aspect_num, min_aspect_den;
		/** Program-specified maximum aspect ratios */
		int32_t max_aspect_num, max_aspect_den;
		/** Program-specified base size */
		int32_t base_width, base_height;
		/** Program-specified window gravity */
		uint32_t win_gravity;
	} WMSizeHints;

	enum WMSizeHintsFlag {
		WM_SIZE_HINT_US_POSITION = 1U << 0,
		WM_SIZE_HINT_US_SIZE = 1U << 1,
		WM_SIZE_HINT_P_POSITION = 1U << 2,
		WM_SIZE_HINT_P_SIZE = 1U << 3,
		WM_SIZE_HINT_P_MIN_SIZE = 1U << 4,
		WM_SIZE_HINT_P_MAX_SIZE = 1U << 5,
		WM_SIZE_HINT_P_RESIZE_INC = 1U << 6,
		WM_SIZE_HINT_P_ASPECT = 1U << 7,
		WM_SIZE_HINT_BASE_SIZE = 1U << 8,
		WM_SIZE_HINT_P_WIN_GRAVITY = 1U << 9
	};

	WMSizeHints hints = {
		.flags = WM_SIZE_HINT_P_MIN_SIZE | WM_SIZE_HINT_P_MAX_SIZE | WM_SIZE_HINT_P_WIN_GRAVITY,
		.win_gravity = XCB_GRAVITY_STATIC,
		.min_width = min_w,
		.min_height = min_h,
		.max_width = max_w,
		.max_height = max_h,
	};

	// CENTER
	hints.flags |= WM_SIZE_HINT_P_WIN_GRAVITY;
	hints.win_gravity = XCB_GRAVITY_CENTER;

	// Position
	/* hints.flags |= WM_SIZE_HINT_P_SIZE; */
	/* hints.x = 0; */
	/* hints.y = 0; */

	xcb_change_property(wm_state->conn, XCB_PROP_MODE_REPLACE, surface->handle,
		XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS,
		32, sizeof(WMSizeHints) >> 2, &hints);

	xcb_query_pointer_reply_t *ptr = xcb_query_pointer_reply(wm_state->conn, xcb_query_pointer(wm_state->conn, wm_state->root.handle), 0);
	int32_t target_x = ptr->root_x; // or align to monitor boundary
	int32_t target_y = ptr->root_y;
	free(ptr);

	// Then use target_x/target_y in configure_window or WM_NORMAL_HINTS

	xcb_configure_window(wm_state->conn, surface->handle, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (uint32_t[]){ target_x, target_y });
	xcb_flush(wm_state->conn);
}

WM_Surface *wm__surface_from_handle(xcb_window_t window) {
	WM_Surface *result = 0;
	for (uint32_t index = 0; index < wm_state->surface_count; ++index) {
		if (wm_state->surfaces[index].handle == window)
			result = &wm_state->surfaces[index];
	}

	return result;
}
