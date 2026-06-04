#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/logger.h"
#include "core/strings.h"
#include "core/input_types.h"

#include "os.h"

#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/xkb.h>
#include <xcb/xinput.h>

#include <linux/input-event-codes.h>

typedef struct {
	xcb_connection_t *conn;
	xcb_window_t root;

	xcb_cursor_t hidden_cursor;

	xcb_atom_t close_atom;

	struct {
		uint8_t opcode; //
	} xinput;

	uint8_t keycodes[KEY_CODE_COUNT];
	uint64_t start_time;
} OS_State;

static OS_State state[1] = { 0 };

typedef struct {
	uint32_t handle;
} OS_Window;
typedef struct {
	bool keys[256];
} InputState;

void create_key_table(void);

xcb_intern_atom_reply_t *os_atom(String name) {
	xcb_intern_atom_reply_t *result = xcb_intern_atom_reply(state->conn, xcb_intern_atom(state->conn, 1, name.length, name.text), 0);
	return result;
}

typedef void (*PFN_game_hookup)(void);

int32_t main(int32_t argc, const char *argv[]) {
	state->conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(state->conn)) {
		LOG_ERROR("failed to connect to X server");
		return -1;
	}
	state->root = xcb_setup_roots_iterator(xcb_get_setup(state->conn)).data->root;
	xcb_xkb_use_extension(state->conn, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);
	xcb_xkb_per_client_flags(state->conn, XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 1, 0, 0, 0);
	InputState input_state = { 0 };

	xcb_pixmap_t cursor_pixmap = xcb_generate_id(state->conn);
	xcb_create_pixmap(
		state->conn,
		1, // Depth
		cursor_pixmap,
		state->root,
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

	LOG_INFO("test.txt %s", os_file_exists(str_lit("test.txt")) ? "exists" : "does not exist");
	LOG_INFO("libgame.so %s", os_file_exists(str_lit("libgame.so")) ? "exists" : "does not exist");

	create_key_table();

	xcb_intern_atom_reply_t *close_reply = os_atom(str_lit("WM_DELETE_WINDOW"));

	if (close_reply == NULL) {
		LOG_INFO("failed to aquire X server protocol.");
		return -1;
	}
	state->close_atom = close_reply->atom;
	free(close_reply);

	OS_File write_handle = os_file_open(str_lit("other.txt"), OS_FILE_MODE_READWRITE);
	os_file_write(write_handle, (void *)"I think TRUNC does what I want (e.g., resize)", sizeof("I think TRUNC does what I want (e.g., resize)") - 1);
	uint64_t size = os_file_size(write_handle);
	os_file_write(write_handle, (void *)"\nThis should be appended", sizeof("\nThis should be appended") - 1);
	os_file_close(write_handle);

	OS_File read_handle = os_file_open(str_lit("other.txt"), OS_FILE_MODE_READ);
	char buffer[12];
	os_file_read(read_handle, buffer, sizeof(buffer) - 1);
	LOG_INFO("READ: %s", buffer);
	os_file_close(read_handle);

	String code = str_lit(
		"#include <stdio.h>\n\n"
		"int game_hook(void) {\n"
		"    printf(\"This was loaded dynamically!\\n\");\n"
		"    return 0;\n"
		"}");
	os_file_write_entire(str_lit("src/game.c"), code.text, code.length);

	ArenaTemp scratch = arena_scratch_begin(NULL);
	String file_content = os_file_read_entire(scratch.arena, str_lit("other.txt"));
	LOG_INFO("FILE_CONTENT: %.*s", sarg(file_content));
	arena_scratch_end(scratch);

	os_directory_delete(str_lit("src"));
	os_directory_delete(str_lit("src"));
	LOG_INFO("src %s", os_directory_exists(str_lit("src")) ? "exists" : "does not exist");
	os_directory_make(str_lit("src"));
	os_directory_delete(str_lit("src"));
	os_directory_make(str_lit("src"));
	LOG_INFO("src %s", os_directory_exists(str_lit("src")) ? "exists" : "does not exist");
	os_directory_make(str_lit("src"));
	LOG_INFO("src %s", os_directory_exists(str_lit("src")) ? "exists" : "does not exist");
	os_directory_delete(str_lit("src"));
	LOG_INFO("src %s", os_directory_exists(str_lit("src")) ? "exists" : "does not exist");
	os_directory_make(str_lit("src"));
	LOG_INFO("src %s", os_directory_exists(str_lit("src")) ? "exists" : "does not exist");

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
	window.handle = xcb_generate_id(state->conn);

	OS_Library game = os_library_load(str_lit("game.so"));
	PFN_game_hookup hookup = NULL;
	os_library_symbol(game, str_lit("game_hook"), &hookup);
	if (hookup)
		hookup();
	os_library_unload(game);

	xcb_create_window(
		state->conn,
		XCB_COPY_FROM_PARENT, // Depth
		window.handle,
		state->root, // parent window
		0, 0, // x, y
		800, 600, // width, height
		0, // border width
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		XCB_COPY_FROM_PARENT,
		value_mask,
		values);
	xcb_map_window(state->conn, window.handle);

	// Change title
	String title = str_lit("Window");
	xcb_change_property(
		state->conn,
		XCB_PROP_MODE_REPLACE,
		window.handle,
		XCB_ATOM_WM_NAME,
		XCB_ATOM_STRING,
		8,
		title.length,
		title.text);
	// Register WM_DELETE_WINDOW
	xcb_change_property(
		state->conn,
		XCB_PROP_MODE_REPLACE,
		window.handle,
		state->close_atom,
		XCB_ATOM_ATOM,
		32,
		1,
		&state->close_atom);
	xcb_flush(state->conn);

	bool is_open = true;
	while (is_open) {
		xcb_generic_event_t *event = NULL;

		while ((event = xcb_poll_for_event(state->conn))) {
			switch (event->response_type & ~0x80) {
				case XCB_CLIENT_MESSAGE: {
					xcb_client_message_event_t *cm = (xcb_client_message_event_t *)event;
					if (cm->data.data32[0] == state->close_atom)
						is_open = false;
				} break;

				case XCB_KEY_PRESS:
				case XCB_KEY_RELEASE: {
					xcb_key_press_event_t *kp = (xcb_key_press_event_t *)event;

					uint32_t scancode = kp->detail - 8;
					if (scancode < 256) {
						bool pressed = (event->response_type & ~0x80) == XCB_KEY_PRESS;
						KeyboardKey key = state->keycodes[scancode];
						bool repeat = pressed & input_state.keys[key];

						input_state.keys[key] = pressed;
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
