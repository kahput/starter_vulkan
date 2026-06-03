#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/logger.h"
#include "core/strings.h"
#include "core/input_types.h"

#include "os.h"

#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/xinput.h>

#include <linux/input-event-codes.h>

typedef struct {
	xcb_connection_t *connection;
	xcb_screen_t *screen;

	xcb_cursor_t hidden_cursor;

	xcb_atom_t close_atom;

	struct {
		uint8_t opcode; //
	} xinput;

	uint8_t keycodes[KEY_CODE_COUNT];
	uint64_t start_time;
} OS_State;

static OS_State os_state[1] = { 0 };

typedef struct {
	uint32_t handle;
} OS_Window;
typedef struct {
	bool keys[256];
} InputState;

void create_key_table(void);

xcb_intern_atom_reply_t *os_atom(String name) {
	xcb_intern_atom_reply_t *result = xcb_intern_atom_reply(os_state->connection, xcb_intern_atom(os_state->connection, 1, name.length, name.text), 0);
	return result;
}

int32_t main(int32_t argc, const char *argv[]) {
	os_state->connection = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(os_state->connection)) {
		LOG_ERROR("failed to connect to X server");
		return -1;
	}
	os_state->screen = xcb_setup_roots_iterator(xcb_get_setup(os_state->connection)).data;
	InputState input_state = { 0 };

	xcb_pixmap_t cursor_pixmap = xcb_generate_id(os_state->connection);
	xcb_create_pixmap(
		os_state->connection,
		1, // Depth
		cursor_pixmap,
		os_state->screen->root,
		1, 1); // Width, height
	os_state->hidden_cursor = xcb_generate_id(os_state->connection);
	xcb_create_cursor(
		os_state->connection,
		os_state->hidden_cursor,
		cursor_pixmap,
		cursor_pixmap,
		0, 0, 0, // Foreground RGB
		0, 0, 0, // Background RGB
		0, 0 // Hotspot X, Y
	);
	xcb_free_pixmap(os_state->connection, cursor_pixmap);

	LOG_INFO("random.h %s", os_file_exists(str_lit("random.h")) ? "exists" : "does not exist");
	LOG_INFO("libgame.so %s", os_file_exists(str_lit("libgame.so")) ? "exists" : "does not exist");

	create_key_table();

	xcb_intern_atom_reply_t *close_reply = os_atom(str_lit("WM_DELETE_WINDOW"));

	if (close_reply == NULL) {
		LOG_INFO("failed to aquire X server protocol.");
		return -1;
	}
	os_state->close_atom = close_reply->atom;
	free(close_reply);

	// XInputExtension setup
	xcb_query_extension_cookie_t ext_cookie =
		xcb_query_extension(os_state->connection, 15, "XInputExtension");
	xcb_query_extension_reply_t *ext_reply =
		xcb_query_extension_reply(os_state->connection, ext_cookie, NULL);

	ASSERT(ext_reply && ext_reply->present);

	os_state->xinput.opcode = ext_reply->major_opcode;
	free(ext_reply);

	xcb_input_xi_query_version_cookie_t ver_cookie =
		xcb_input_xi_query_version(os_state->connection, 2, 0);
	xcb_input_xi_query_version_reply_t *ver_reply =
		xcb_input_xi_query_version_reply(os_state->connection, ver_cookie, NULL);
	free(ver_reply);

	// Xinput
	struct {
		xcb_input_event_mask_t head;
		xcb_input_xi_event_mask_t mask;
	} xinput_event_mask = {
		.head = {
		  .deviceid = XCB_INPUT_DEVICE_ALL,
		  .mask_len = 1,
		},
		.mask = XCB_INPUT_XI_EVENT_MASK_RAW_MOTION
	};

	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(os_state->connection)).data;
	xcb_input_xi_select_events(os_state->connection, screen->root, 1, &xinput_event_mask.head);
	xcb_flush(os_state->connection);

	FileHandle handle = os_file_open(str_lit("/home/kahput/test.c"), OS_FILE_MODE_READWRITE);
    os_file_close(handle);
    os_file_close(handle);


	uint32_t event_mask =
		XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION |
		XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE;

	uint32_t value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	uint32_t values[] = {
		0x00aade87,
		event_mask
	};

	OS_Window window = { 0 };
	window.handle = xcb_generate_id(os_state->connection);

	xcb_create_window(
		os_state->connection,
		XCB_COPY_FROM_PARENT, // Depth
		window.handle,
		os_state->screen->root, // parent window
		0, 0, // x, y
		800, 600, // width, height
		0, // border width
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		XCB_COPY_FROM_PARENT,
		value_mask,
		values);
	xcb_map_window(os_state->connection, window.handle);

	// Change title
	String title = str_lit("Window");
	xcb_change_property(
		os_state->connection,
		XCB_PROP_MODE_REPLACE,
		window.handle,
		XCB_ATOM_WM_NAME,
		XCB_ATOM_STRING,
		8,
		title.length,
		title.text);
	// Register WM_DELETE_WINDOW
	xcb_change_property(
		os_state->connection,
		XCB_PROP_MODE_REPLACE,
		window.handle,
		os_state->close_atom,
		XCB_ATOM_ATOM,
		32,
		1,
		&os_state->close_atom);
	xcb_flush(os_state->connection);

	bool is_open = true;
	while (is_open) {
		xcb_generic_event_t *event = NULL;

		while ((event = xcb_poll_for_event(os_state->connection))) {
			switch (event->response_type & ~0x80) {
				case XCB_CLIENT_MESSAGE: {
					xcb_client_message_event_t *cm = (xcb_client_message_event_t *)event;
					if (cm->data.data32[0] == os_state->close_atom)
						is_open = false;
				} break;

				case XCB_KEY_PRESS:
				case XCB_KEY_RELEASE: {
					xcb_key_press_event_t *kp = (xcb_key_press_event_t *)event;

					uint32_t scancode = kp->detail - 8;

					if (scancode < 256) {
						bool pressed = (event->response_type & ~0x80) == XCB_KEY_PRESS;

						input_state.keys[os_state->keycodes[scancode]] = pressed;
					}
				} break;
			}
		}

		if (input_state.keys[KEY_CODE_ESCAPE])
			is_open = false;
	}

	return 0;
}

void create_key_table(void) {
	os_state->keycodes[KEY_GRAVE] = KEY_CODE_GRAVE;
	os_state->keycodes[KEY_1] = KEY_CODE_1;
	os_state->keycodes[KEY_2] = KEY_CODE_2;
	os_state->keycodes[KEY_3] = KEY_CODE_3;
	os_state->keycodes[KEY_4] = KEY_CODE_4;
	os_state->keycodes[KEY_5] = KEY_CODE_5;
	os_state->keycodes[KEY_6] = KEY_CODE_6;
	os_state->keycodes[KEY_7] = KEY_CODE_7;
	os_state->keycodes[KEY_8] = KEY_CODE_8;
	os_state->keycodes[KEY_9] = KEY_CODE_9;
	os_state->keycodes[KEY_0] = KEY_CODE_0;
	os_state->keycodes[KEY_SPACE] = KEY_CODE_SPACE;
	os_state->keycodes[KEY_MINUS] = KEY_CODE_MINUS;
	os_state->keycodes[KEY_EQUAL] = KEY_CODE_EQUAL;
	os_state->keycodes[KEY_Q] = KEY_CODE_Q;
	os_state->keycodes[KEY_W] = KEY_CODE_W;
	os_state->keycodes[KEY_E] = KEY_CODE_E;
	os_state->keycodes[KEY_R] = KEY_CODE_R;
	os_state->keycodes[KEY_T] = KEY_CODE_T;
	os_state->keycodes[KEY_Y] = KEY_CODE_Y;
	os_state->keycodes[KEY_U] = KEY_CODE_U;
	os_state->keycodes[KEY_I] = KEY_CODE_I;
	os_state->keycodes[KEY_O] = KEY_CODE_O;
	os_state->keycodes[KEY_P] = KEY_CODE_P;
	os_state->keycodes[KEY_LEFTBRACE] = KEY_CODE_LEFTBRACKET;
	os_state->keycodes[KEY_RIGHTBRACE] = KEY_CODE_RIGHTBRACKET;
	os_state->keycodes[KEY_A] = KEY_CODE_A;
	os_state->keycodes[KEY_S] = KEY_CODE_S;
	os_state->keycodes[KEY_D] = KEY_CODE_D;
	os_state->keycodes[KEY_F] = KEY_CODE_F;
	os_state->keycodes[KEY_G] = KEY_CODE_G;
	os_state->keycodes[KEY_H] = KEY_CODE_H;
	os_state->keycodes[KEY_J] = KEY_CODE_J;
	os_state->keycodes[KEY_K] = KEY_CODE_K;
	os_state->keycodes[KEY_L] = KEY_CODE_L;
	os_state->keycodes[KEY_SEMICOLON] = KEY_CODE_SEMICOLON;
	os_state->keycodes[KEY_APOSTROPHE] = KEY_CODE_APOSTROPHE;
	os_state->keycodes[KEY_Z] = KEY_CODE_Z;
	os_state->keycodes[KEY_X] = KEY_CODE_X;
	os_state->keycodes[KEY_C] = KEY_CODE_C;
	os_state->keycodes[KEY_V] = KEY_CODE_V;
	os_state->keycodes[KEY_B] = KEY_CODE_B;
	os_state->keycodes[KEY_N] = KEY_CODE_N;
	os_state->keycodes[KEY_M] = KEY_CODE_M;
	os_state->keycodes[KEY_COMMA] = KEY_CODE_COMMA;
	os_state->keycodes[KEY_DOT] = KEY_CODE_PERIOD;
	os_state->keycodes[KEY_SLASH] = KEY_CODE_SLASH;
	os_state->keycodes[KEY_BACKSLASH] = KEY_CODE_BACKSLASH;
	os_state->keycodes[KEY_ESC] = KEY_CODE_ESCAPE;
	os_state->keycodes[KEY_TAB] = KEY_CODE_TAB;
	os_state->keycodes[KEY_LEFTSHIFT] = KEY_CODE_LEFTSHIFT;
	os_state->keycodes[KEY_RIGHTSHIFT] = KEY_CODE_RIGHTSHIFT;
	os_state->keycodes[KEY_LEFTCTRL] = KEY_CODE_LEFTCTRL;
	os_state->keycodes[KEY_RIGHTCTRL] = KEY_CODE_RIGHTCTRL;
	os_state->keycodes[KEY_LEFTALT] = KEY_CODE_LEFTALT;
	os_state->keycodes[KEY_RIGHTALT] = KEY_CODE_RIGHTALT;
	os_state->keycodes[KEY_LEFTMETA] = KEY_CODE_LEFTMETA;
	os_state->keycodes[KEY_RIGHTMETA] = KEY_CODE_RIGHTMETA;
	os_state->keycodes[KEY_COMPOSE] = KEY_CODE_MENU;
	os_state->keycodes[KEY_NUMLOCK] = KEY_CODE_NUMLOCK;
	os_state->keycodes[KEY_CAPSLOCK] = KEY_CODE_CAPSLOCK;
	os_state->keycodes[KEY_PRINT] = KEY_CODE_PRINT;
	os_state->keycodes[KEY_SCROLLLOCK] = KEY_CODE_SCROLLLOCK;
	os_state->keycodes[KEY_PAUSE] = KEY_CODE_PAUSE;
	os_state->keycodes[KEY_DELETE] = KEY_CODE_DELETE;
	os_state->keycodes[KEY_BACKSPACE] = KEY_CODE_BACKSPACE;
	os_state->keycodes[KEY_ENTER] = KEY_CODE_ENTER;
	os_state->keycodes[KEY_HOME] = KEY_CODE_HOME;
	os_state->keycodes[KEY_END] = KEY_CODE_END;
	os_state->keycodes[KEY_PAGEUP] = KEY_CODE_PAGEUP;
	os_state->keycodes[KEY_PAGEDOWN] = KEY_CODE_PAGEDOWN;
	os_state->keycodes[KEY_INSERT] = KEY_CODE_INSERT;
	os_state->keycodes[KEY_LEFT] = KEY_CODE_LEFT;
	os_state->keycodes[KEY_RIGHT] = KEY_CODE_RIGHT;
	os_state->keycodes[KEY_DOWN] = KEY_CODE_DOWN;
	os_state->keycodes[KEY_UP] = KEY_CODE_UP;
	os_state->keycodes[KEY_F1] = KEY_CODE_F1;
	os_state->keycodes[KEY_F2] = KEY_CODE_F2;
	os_state->keycodes[KEY_F3] = KEY_CODE_F3;
	os_state->keycodes[KEY_F4] = KEY_CODE_F4;
	os_state->keycodes[KEY_F5] = KEY_CODE_F5;
	os_state->keycodes[KEY_F6] = KEY_CODE_F6;
	os_state->keycodes[KEY_F7] = KEY_CODE_F7;
	os_state->keycodes[KEY_F8] = KEY_CODE_F8;
	os_state->keycodes[KEY_F9] = KEY_CODE_F9;
	os_state->keycodes[KEY_F10] = KEY_CODE_F10;
	os_state->keycodes[KEY_F11] = KEY_CODE_F11;
	os_state->keycodes[KEY_F12] = KEY_CODE_F12;
	os_state->keycodes[KEY_F13] = KEY_CODE_F13;
	os_state->keycodes[KEY_F14] = KEY_CODE_F14;
	os_state->keycodes[KEY_F15] = KEY_CODE_F15;
	os_state->keycodes[KEY_F16] = KEY_CODE_F16;
	os_state->keycodes[KEY_F17] = KEY_CODE_F17;
	os_state->keycodes[KEY_F18] = KEY_CODE_F18;
	os_state->keycodes[KEY_F19] = KEY_CODE_F19;
	os_state->keycodes[KEY_F20] = KEY_CODE_F20;
	os_state->keycodes[KEY_F21] = KEY_CODE_F21;
	os_state->keycodes[KEY_F22] = KEY_CODE_F22;
	os_state->keycodes[KEY_F23] = KEY_CODE_F23;
	os_state->keycodes[KEY_F24] = KEY_CODE_F24;
	os_state->keycodes[KEY_KPSLASH] = KEY_CODE_KPSLASH;
	os_state->keycodes[KEY_KPASTERISK] = KEY_CODE_KPASTERISK;
	os_state->keycodes[KEY_KPMINUS] = KEY_CODE_KPMINUS;
	os_state->keycodes[KEY_KPPLUS] = KEY_CODE_KPPLUS;
	os_state->keycodes[KEY_KP0] = KEY_CODE_KP0;
	os_state->keycodes[KEY_KP1] = KEY_CODE_KP1;
	os_state->keycodes[KEY_KP2] = KEY_CODE_KP2;
	os_state->keycodes[KEY_KP3] = KEY_CODE_KP3;
	os_state->keycodes[KEY_KP4] = KEY_CODE_KP4;
	os_state->keycodes[KEY_KP5] = KEY_CODE_KP5;
	os_state->keycodes[KEY_KP6] = KEY_CODE_KP6;
	os_state->keycodes[KEY_KP7] = KEY_CODE_KP7;
	os_state->keycodes[KEY_KP8] = KEY_CODE_KP8;
	os_state->keycodes[KEY_KP9] = KEY_CODE_KP9;
	os_state->keycodes[KEY_KPDOT] = KEY_CODE_KPDOT;
	os_state->keycodes[KEY_KPEQUAL] = KEY_CODE_KPEQUAL;
	os_state->keycodes[KEY_KPENTER] = KEY_CODE_KPENTER;
	os_state->keycodes[KEY_102ND] = KEY_CODE_WORLD_1;
}
