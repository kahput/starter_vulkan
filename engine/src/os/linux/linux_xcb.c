#include "common.h"
#include "core/debug.h"
#include "core/input_types.h"
#include "core/logger.h"
#include "os.h"

#include <stdlib.h>
#include <xcb/xcb.h>
/* #include <xcb/xcb_icccm.h> // sizing hints */
#include <linux/input-event-codes.h>

// EXTENSIONS
#include <xcb/xkb.h> // disable virtual release
#include <xcb/xproto.h>

struct OS_Surface {
	uint32_t handle;
	uint32_t width, height;

	int32x2 virtual_cursor;
	bool cursor_captured, warping;
};

typedef struct {
	xcb_connection_t *conn;
	OS_Surface root;

	xcb_cursor_t hidden_cursor;

	xcb_atom_t wm_protocols_atom;
	xcb_atom_t wm_delete_window_atom;
	xcb_atom_t wm_transient_for_atom;
	xcb_atom_t wm_window_type;

	uint8_t keycodes[KEY_CODE_COUNT];
	uint64_t start_time;

	OS_Surface surfaces[32];
	uint32_t count;

	bool initialized;
} OS_DisplayState;

static OS_DisplayState state[1];

static inline xcb_intern_atom_reply_t *os__atom(String8 name);
static inline void create_key_table(void);
static inline void os__surface_set_min_max(OS_Surface *surface, uint32_t min_w, uint32_t min_h, uint32_t max_w, uint32_t max_h);
static inline OS_Surface *os__surface_from_handle(xcb_window_t window);

typedef enum {
	ATOM_PROTOCOL,
	ATOM_CLOSE,
	ATOM_TRANSIENT,
	ATOM_WINDOW_STATE,
	ATOM_WINDOW_TYPE,
} Atom;

bool os_display_startup(void) {
	state->conn = xcb_connect(0, 0);
	if (xcb_connection_has_error(state->conn)) {
		LOG_ERROR("os_display_startup - failed to connect to X server");
		return false;
	}
	state->root.handle = xcb_setup_roots_iterator(xcb_get_setup(state->conn)).data->root;
	xcb_xkb_use_extension(state->conn, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);
	xcb_xkb_per_client_flags(state->conn, XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 1, 0, 0, 0);

	xcb_pixmap_t cursor_pixmap = xcb_generate_id(state->conn);
	xcb_create_pixmap(
		state->conn,
		1, // Depth
		cursor_pixmap,
		state->root.handle,
		1, 1); // Width, height
	state->hidden_cursor = xcb_generate_id(state->conn);
	xcb_create_cursor(
		state->conn,
		state->hidden_cursor,
		cursor_pixmap,
		cursor_pixmap,
		0, 0, 0, // Foreground RGB
		0, 0, 0, // Background RGB
		0, 0 // Hotspot X, Y
	);
	xcb_free_pixmap(state->conn, cursor_pixmap);

	create_key_table();

	const char *atom_strings[] = {
		"WM_PROTOCOLS",
		"WM_DELETE_WINDOW"
		"WM_TRANSIENT_FOR"
		"_NET_WM_WINDOW_TYPE"
	};

	xcb_intern_atom_reply_t *proto_reply = os__atom(s("WM_PROTOCOLS"));
	xcb_intern_atom_reply_t *close_reply = os__atom(s("WM_DELETE_WINDOW"));
	xcb_intern_atom_reply_t *transient_reply = os__atom(s("WM_TRANSIENT_FOR"));
	xcb_intern_atom_reply_t *type_reply = os__atom(s("_NET_WM_WINDOW_TYPE"));
	xcb_intern_atom_reply_t *state_reply = os__atom(s("_NET_WM_STATE"));

	if (proto_reply == 0 ||
		close_reply == 0 ||
		transient_reply == 0 ||
		type_reply == 0 ||
		state_reply == 0) {
		LOG_INFO("failed to aquire X server protocol.");
		return -1;
	}

	state->wm_protocols_atom = proto_reply->atom;
	state->wm_delete_window_atom = close_reply->atom;
	state->wm_transient_for_atom = transient_reply->atom;
	state->wm_window_type = type_reply->atom;

	free(close_reply);
	free(proto_reply);
	free(transient_reply);
	free(type_reply);

	state->initialized = true;
	return true;
}

void os_display_shutdown(void) {
	if (state->conn)
		xcb_disconnect(state->conn);
}

OS_Surface *os_surface_open(uint32_t width, uint32_t height, String8 title, OS_SurfaceFlags flags) {
	OS_Surface *result = os_surface_open_with_parent(&state->root, width, height, title, flags);
	return result;
}

OS_Surface *os_surface_open_with_parent(OS_Surface *parent, uint32_t width, uint32_t height, String8 title, OS_SurfaceFlags flags) {
	if (state->initialized == false) {
		LOG_WARN("%s - display must be initialized before surface creation, call os_display_startup.", __func__);
		return 0;
	}
	uint32_t event_mask =
		XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION |
		XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE |
		XCB_EVENT_MASK_VISIBILITY_CHANGE;

	uint32_t value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	uint32_t values[] = {
		0x00aade87,
		event_mask
	};

	OS_Surface *surface = &state->surfaces[state->count++];
	surface->width = width;
	surface->height = height;

	surface->handle = xcb_generate_id(state->conn);

	xcb_create_window(
		state->conn,
		XCB_COPY_FROM_PARENT, // Depth
		surface->handle,
		state->root.handle, // parent window
		0, 0, // x, y
		surface->width, surface->height, // width, height
		0, // border width
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		XCB_COPY_FROM_PARENT,
		value_mask,
		values);
	if (has_flag(flags, OS_SURFACE_FLAG_RESIZEABLE) == false)
		os__surface_set_min_max(surface, surface->width, surface->height, width, height);

	// Change title
	xcb_change_property(
		state->conn,
		XCB_PROP_MODE_REPLACE,
		surface->handle,
		XCB_ATOM_WM_NAME,
		XCB_ATOM_STRING,
		8,
		title.length,
		title.text);
	// Register WM_DELETE_WINDOW
	xcb_change_property(
		state->conn,
		XCB_PROP_MODE_REPLACE,
		surface->handle,
		state->wm_delete_window_atom,
		XCB_ATOM_ATOM,
		32,
		1,
		&state->wm_delete_window_atom);

	xcb_map_window(state->conn, surface->handle);
	xcb_flush(state->conn);
	return surface;
}

void os_surface_close(OS_Surface *surface) {
	xcb_destroy_window(state->conn, surface->handle);
	xcb_flush(state->conn);
}
bool os_surface_valid(OS_Surface *surface);

void os_surface_show(OS_Surface *surface) {
	xcb_map_window(state->conn, surface->handle);
	xcb_flush(state->conn);
}
void os_surface_hide(OS_Surface *surface) {
	xcb_unmap_window(state->conn, surface->handle);
	xcb_flush(state->conn);
}

bool os_event_poll(OS_Event *dst) {
	xcb_generic_event_t *src = xcb_poll_for_event(state->conn);
	if (src == 0)
		return false;

	memory_zero(dst, sizeof(OS_Event));
	switch (src->response_type & ~0x80) {
		case XCB_CLIENT_MESSAGE: {
			xcb_client_message_event_t *cm = (xcb_client_message_event_t *)src;
			if (cm->data.data32[0] == state->wm_delete_window_atom) {
				dst->surface = os__surface_from_handle(cm->window);
				if (dst->surface == 0)
					ASSERT_MESSAGE(false, "The event doesn't belong to any window");
				dst->type = OS_EVENT_TYPE_SURFACE_CLOSE;
			}
		} break;

		case XCB_CONFIGURE_NOTIFY: {
			xcb_configure_notify_event_t *cfg = (xcb_configure_notify_event_t *)src;

			OS_Surface *surface = os__surface_from_handle(cfg->window);
			if (surface && (surface->width != cfg->width || surface->height != cfg->height)) {
				dst->type = OS_EVENT_TYPE_SURFACE_RESIZE;
				dst->surface = surface;

				surface->width = cfg->width;
				surface->height = cfg->height;

				dst->as.resize.width = surface->width;
				dst->as.resize.height = surface->height;
			}
		} break;

		case XCB_KEY_PRESS:
		case XCB_KEY_RELEASE: {
			xcb_key_press_event_t *kp = (xcb_key_press_event_t *)src;

			uint32_t scancode = kp->detail - 8;
			if (scancode < 256) {
				bool released = (src->response_type & ~0x80) == XCB_KEY_RELEASE;
				KeyboardKey key = state->keycodes[scancode];

				dst->type = OS_EVENT_TYPE_KEY_PRESS + released;
				dst->as.key.key_code = key;
			}
		} break;
		case XCB_BUTTON_PRESS:
		case XCB_BUTTON_RELEASE: {
			xcb_button_press_event_t *bp = (xcb_button_press_event_t *)src;

			if (bp->detail <= 3) {
				bool released = (src->response_type & ~0x80) == XCB_BUTTON_RELEASE;
				dst->surface = os__surface_from_handle(bp->event);
				dst->type = OS_EVENT_TYPE_MOUSE_PRESS + released;
				dst->as.mouse_button.x = bp->event_x;
				dst->as.mouse_button.y = bp->event_y;
				dst->as.mouse_button.button =
					bp->detail == 1
					? MOUSE_BUTTON_LEFT
					: bp->detail == 2
					? MOUSE_BUTTON_MIDDLE
					: MOUSE_BUTTON_RIGHT;
			}
		} break;
		case XCB_MOTION_NOTIFY: {
			xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)src;
			OS_Surface *surface = os__surface_from_handle(motion->event);

			if (surface->cursor_captured) {
				if (surface->warping) {
					surface->warping = false;
					break;
				}

				int32_t cx = surface->width / 2;
				int32_t cy = surface->height / 2;
				int32_t dx = (int32_t)motion->event_x - cx;
				int32_t dy = (int32_t)motion->event_y - cy;
				surface->virtual_cursor.x += dx;
				surface->virtual_cursor.y += dy;

				if (dx != 0 || dy != 0) {
					surface->warping = true;
					xcb_warp_pointer(state->conn, XCB_NONE, surface->handle,
						0, 0, 0, 0, cx, cy);
					xcb_flush(state->conn);
				}

				motion->event_x = surface->virtual_cursor.x;
				motion->event_y = surface->virtual_cursor.y;
			}

			dst->type = OS_EVENT_TYPE_MOUSE_MOVE;
			dst->surface = surface;
			dst->as.mouse_move.x = motion->event_x;
			dst->as.mouse_move.y = motion->event_y;
		} break;
	}
	free(src);

	return true;
}

void os_surface_set_min(OS_Surface *surface, uint32_t width, uint32_t height);
void os_surface_set_max(OS_Surface *surface, uint32_t width, uint32_t height);

float os_surface_dpi(OS_Surface *surface) {
	return 1.0f;
}
uint2 os_surface_size(OS_Surface *surface) {
	return (uint32x2){ surface->width, surface->height };
}

/* void os_cursor_show(bool show); */
/* void os_cursor_capture(OS_Surface *surface, bool capture); */
/* void os_cursor_set_position(OS_Surface *surface, int32_t x, int32_t y); */

static const char *extensions[] = {
	"VK_KHR_surface",
	"VK_KHR_xcb_surface",
};
const char **os_surface_vulkan_extensions(uint32_t *count) {
	*count = countof(extensions);
	return extensions;
}

void *os_native_display_handle(void) {
	return (void *)state->conn;
}
void *os_native_surface_handle(OS_Surface *surface) {
	return (void *)(uint64_t)surface->handle;
}

void os_cursor_show(OS_Surface *surface, bool show) {
	if (show) {
		uint32_t value = XCB_NONE;
		xcb_change_window_attributes(state->conn, surface->handle, XCB_CW_CURSOR, &value);
	} else
		xcb_change_window_attributes(state->conn, surface->handle, XCB_CW_CURSOR, &state->hidden_cursor);

	xcb_flush(state->conn);
}

void os_cursor_capture(OS_Surface *surface, bool capture) {
	if (capture == surface->cursor_captured)
		return;
	surface->cursor_captured = capture;

	if (capture) {
		xcb_grab_pointer_cookie_t cookie = xcb_grab_pointer(
			state->conn,
			0, // owner_events: don't redirect events to grab window
			surface->handle,
			XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
			XCB_GRAB_MODE_ASYNC,
			XCB_GRAB_MODE_ASYNC,
			surface->handle, // confine to this window
			state->hidden_cursor, // change cursor - XCB_NONE for normal
			XCB_TIME_CURRENT_TIME);
		xcb_grab_pointer_reply_t *reply = xcb_grab_pointer_reply(state->conn, cookie, NULL);

		surface->virtual_cursor.x = surface->width / 2;
		surface->virtual_cursor.y = surface->height / 2;

		xcb_warp_pointer(state->conn, XCB_NONE, surface->handle,
			0, 0, 0, 0, surface->virtual_cursor.x, surface->virtual_cursor.y);
		surface->warping = true; // flag to swallow the warp event

		if (reply->status != XCB_GRAB_STATUS_SUCCESS)
			surface->cursor_captured = false;
		free(reply);
	} else
		xcb_ungrab_pointer(state->conn, XCB_TIME_CURRENT_TIME);

	xcb_flush(state->conn);
}
bool os_cursor_captured(OS_Surface *surface) {
	return surface->cursor_captured;
}

void os_cursor_set_position(OS_Surface *surface, int32_t x, int32_t y) {
	xcb_warp_pointer(state->conn, XCB_NONE, surface->handle, 0, 0, 0, 0, x, y);
	surface->warping = true;
	xcb_flush(state->conn);
}

static inline xcb_intern_atom_reply_t *os__atom(String8 name) {
	xcb_intern_atom_reply_t *result = xcb_intern_atom_reply(state->conn, xcb_intern_atom(state->conn, 1, name.length, (char *)name.text), 0);
	return result;
}

void create_key_table(void) {
	state->keycodes[KEY_GRAVE] = KEY_CODE_GRAVE;
	state->keycodes[KEY_1] = KEY_CODE_1;
	state->keycodes[KEY_2] = KEY_CODE_2;
	state->keycodes[KEY_3] = KEY_CODE_3;
	state->keycodes[KEY_4] = KEY_CODE_4;
	state->keycodes[KEY_5] = KEY_CODE_5;
	state->keycodes[KEY_6] = KEY_CODE_6;
	state->keycodes[KEY_7] = KEY_CODE_7;
	state->keycodes[KEY_8] = KEY_CODE_8;
	state->keycodes[KEY_9] = KEY_CODE_9;
	state->keycodes[KEY_0] = KEY_CODE_0;
	state->keycodes[KEY_SPACE] = KEY_CODE_SPACE;
	state->keycodes[KEY_MINUS] = KEY_CODE_MINUS;
	state->keycodes[KEY_EQUAL] = KEY_CODE_EQUAL;
	state->keycodes[KEY_Q] = KEY_CODE_Q;
	state->keycodes[KEY_W] = KEY_CODE_W;
	state->keycodes[KEY_E] = KEY_CODE_E;
	state->keycodes[KEY_R] = KEY_CODE_R;
	state->keycodes[KEY_T] = KEY_CODE_T;
	state->keycodes[KEY_Y] = KEY_CODE_Y;
	state->keycodes[KEY_U] = KEY_CODE_U;
	state->keycodes[KEY_I] = KEY_CODE_I;
	state->keycodes[KEY_O] = KEY_CODE_O;
	state->keycodes[KEY_P] = KEY_CODE_P;
	state->keycodes[KEY_LEFTBRACE] = KEY_CODE_LEFTBRACKET;
	state->keycodes[KEY_RIGHTBRACE] = KEY_CODE_RIGHTBRACKET;
	state->keycodes[KEY_A] = KEY_CODE_A;
	state->keycodes[KEY_S] = KEY_CODE_S;
	state->keycodes[KEY_D] = KEY_CODE_D;
	state->keycodes[KEY_F] = KEY_CODE_F;
	state->keycodes[KEY_G] = KEY_CODE_G;
	state->keycodes[KEY_H] = KEY_CODE_H;
	state->keycodes[KEY_J] = KEY_CODE_J;
	state->keycodes[KEY_K] = KEY_CODE_K;
	state->keycodes[KEY_L] = KEY_CODE_L;
	state->keycodes[KEY_SEMICOLON] = KEY_CODE_SEMICOLON;
	state->keycodes[KEY_APOSTROPHE] = KEY_CODE_APOSTROPHE;
	state->keycodes[KEY_Z] = KEY_CODE_Z;
	state->keycodes[KEY_X] = KEY_CODE_X;
	state->keycodes[KEY_C] = KEY_CODE_C;
	state->keycodes[KEY_V] = KEY_CODE_V;
	state->keycodes[KEY_B] = KEY_CODE_B;
	state->keycodes[KEY_N] = KEY_CODE_N;
	state->keycodes[KEY_M] = KEY_CODE_M;
	state->keycodes[KEY_COMMA] = KEY_CODE_COMMA;
	state->keycodes[KEY_DOT] = KEY_CODE_PERIOD;
	state->keycodes[KEY_SLASH] = KEY_CODE_SLASH;
	state->keycodes[KEY_BACKSLASH] = KEY_CODE_BACKSLASH;
	state->keycodes[KEY_ESC] = KEY_CODE_ESCAPE;
	state->keycodes[KEY_TAB] = KEY_CODE_TAB;
	state->keycodes[KEY_LEFTSHIFT] = KEY_CODE_LEFTSHIFT;
	state->keycodes[KEY_RIGHTSHIFT] = KEY_CODE_RIGHTSHIFT;
	state->keycodes[KEY_LEFTCTRL] = KEY_CODE_LEFTCTRL;
	state->keycodes[KEY_RIGHTCTRL] = KEY_CODE_RIGHTCTRL;
	state->keycodes[KEY_LEFTALT] = KEY_CODE_LEFTALT;
	state->keycodes[KEY_RIGHTALT] = KEY_CODE_RIGHTALT;
	state->keycodes[KEY_LEFTMETA] = KEY_CODE_LEFTMETA;
	state->keycodes[KEY_RIGHTMETA] = KEY_CODE_RIGHTMETA;
	state->keycodes[KEY_COMPOSE] = KEY_CODE_MENU;
	state->keycodes[KEY_NUMLOCK] = KEY_CODE_NUMLOCK;
	state->keycodes[KEY_CAPSLOCK] = KEY_CODE_CAPSLOCK;
	state->keycodes[KEY_PRINT] = KEY_CODE_PRINT;
	state->keycodes[KEY_SCROLLLOCK] = KEY_CODE_SCROLLLOCK;
	state->keycodes[KEY_PAUSE] = KEY_CODE_PAUSE;
	state->keycodes[KEY_DELETE] = KEY_CODE_DELETE;
	state->keycodes[KEY_BACKSPACE] = KEY_CODE_BACKSPACE;
	state->keycodes[KEY_ENTER] = KEY_CODE_ENTER;
	state->keycodes[KEY_HOME] = KEY_CODE_HOME;
	state->keycodes[KEY_END] = KEY_CODE_END;
	state->keycodes[KEY_PAGEUP] = KEY_CODE_PAGEUP;
	state->keycodes[KEY_PAGEDOWN] = KEY_CODE_PAGEDOWN;
	state->keycodes[KEY_INSERT] = KEY_CODE_INSERT;
	state->keycodes[KEY_LEFT] = KEY_CODE_LEFT;
	state->keycodes[KEY_RIGHT] = KEY_CODE_RIGHT;
	state->keycodes[KEY_DOWN] = KEY_CODE_DOWN;
	state->keycodes[KEY_UP] = KEY_CODE_UP;
	state->keycodes[KEY_F1] = KEY_CODE_F1;
	state->keycodes[KEY_F2] = KEY_CODE_F2;
	state->keycodes[KEY_F3] = KEY_CODE_F3;
	state->keycodes[KEY_F4] = KEY_CODE_F4;
	state->keycodes[KEY_F5] = KEY_CODE_F5;
	state->keycodes[KEY_F6] = KEY_CODE_F6;
	state->keycodes[KEY_F7] = KEY_CODE_F7;
	state->keycodes[KEY_F8] = KEY_CODE_F8;
	state->keycodes[KEY_F9] = KEY_CODE_F9;
	state->keycodes[KEY_F10] = KEY_CODE_F10;
	state->keycodes[KEY_F11] = KEY_CODE_F11;
	state->keycodes[KEY_F12] = KEY_CODE_F12;
	state->keycodes[KEY_F13] = KEY_CODE_F13;
	state->keycodes[KEY_F14] = KEY_CODE_F14;
	state->keycodes[KEY_F15] = KEY_CODE_F15;
	state->keycodes[KEY_F16] = KEY_CODE_F16;
	state->keycodes[KEY_F17] = KEY_CODE_F17;
	state->keycodes[KEY_F18] = KEY_CODE_F18;
	state->keycodes[KEY_F19] = KEY_CODE_F19;
	state->keycodes[KEY_F20] = KEY_CODE_F20;
	state->keycodes[KEY_F21] = KEY_CODE_F21;
	state->keycodes[KEY_F22] = KEY_CODE_F22;
	state->keycodes[KEY_F23] = KEY_CODE_F23;
	state->keycodes[KEY_F24] = KEY_CODE_F24;
	state->keycodes[KEY_KPSLASH] = KEY_CODE_KPSLASH;
	state->keycodes[KEY_KPASTERISK] = KEY_CODE_KPASTERISK;
	state->keycodes[KEY_KPMINUS] = KEY_CODE_KPMINUS;
	state->keycodes[KEY_KPPLUS] = KEY_CODE_KPPLUS;
	state->keycodes[KEY_KP0] = KEY_CODE_KP0;
	state->keycodes[KEY_KP1] = KEY_CODE_KP1;
	state->keycodes[KEY_KP2] = KEY_CODE_KP2;
	state->keycodes[KEY_KP3] = KEY_CODE_KP3;
	state->keycodes[KEY_KP4] = KEY_CODE_KP4;
	state->keycodes[KEY_KP5] = KEY_CODE_KP5;
	state->keycodes[KEY_KP6] = KEY_CODE_KP6;
	state->keycodes[KEY_KP7] = KEY_CODE_KP7;
	state->keycodes[KEY_KP8] = KEY_CODE_KP8;
	state->keycodes[KEY_KP9] = KEY_CODE_KP9;
	state->keycodes[KEY_KPDOT] = KEY_CODE_KPDOT;
	state->keycodes[KEY_KPEQUAL] = KEY_CODE_KPEQUAL;
	state->keycodes[KEY_KPENTER] = KEY_CODE_KPENTER;
	state->keycodes[KEY_102ND] = KEY_CODE_WORLD_1;
}
void os__surface_set_min_max(OS_Surface *surface, uint32_t min_w, uint32_t min_h, uint32_t max_w, uint32_t max_h) {
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

	xcb_change_property(state->conn, XCB_PROP_MODE_REPLACE, surface->handle,
		XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS,
		32, sizeof(WMSizeHints) >> 2, &hints);

	xcb_query_pointer_reply_t *ptr = xcb_query_pointer_reply(state->conn, xcb_query_pointer(state->conn, state->root.handle), 0);
	int32_t target_x = ptr->root_x; // or align to monitor boundary
	int32_t target_y = ptr->root_y;
	free(ptr);

	// Then use target_x/target_y in configure_window or WM_NORMAL_HINTS

	xcb_configure_window(state->conn, surface->handle, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, (uint32_t[]){ target_x, target_y });
	xcb_flush(state->conn);
}

OS_Surface *os__surface_from_handle(xcb_window_t window) {
	OS_Surface *result = 0;
	for (uint32_t index = 0; index < state->count; ++index) {
		if (state->surfaces[index].handle == window)
			result = &state->surfaces[index];
	}

	return result;
}
