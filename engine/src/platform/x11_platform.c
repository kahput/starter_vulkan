#include "event.h"
#include "events/platform_events.h"
#include "platform/internal.h"

#include "core/logger.h"
#include <string.h>
#include <xcb/xproto.h>

#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>

#include "common.h"
#include <dlfcn.h>

#include <linux/input-event-codes.h>

#include <stdlib.h>

#define XCB_COUNT sizeof(((X11Platform *)0)->xcb) / sizeof(void *)

static const char *extensions[] = {
	"VK_KHR_surface",
	"VK_KHR_xcb_surface"
};

// Definition of the global library struct
X11Library _x11_library = { 0 };

static void create_key_table(X11Platform *platform);

bool platform_init_x11(Platform *platform) {
	// Try to load the runtime library first (libxcb.so.1), fallback to dev symlink (libxcb.so)
	void *handle = dlopen("libxcb.so.1", RTLD_LAZY);
	if (handle == NULL) {
		handle = dlopen("libxcb.so", RTLD_LAZY);
		if (handle == NULL)
			return false;
	}

	struct platform_internal *internal = (struct platform_internal *)platform->internal;

	_x11_library.handle = handle;

	// --- Core Connection ---
	*(void **)(&_x11_library.connect) = dlsym(handle, "xcb_connect");
	*(void **)(&_x11_library.disconnect) = dlsym(handle, "xcb_disconnect");

	// --- Setup & Roots ---
	*(void **)(&_x11_library.get_setup) = dlsym(handle, "xcb_get_setup");
	*(void **)(&_x11_library.setup_roots_iterator) = dlsym(handle, "xcb_setup_roots_iterator");

	// --- Atoms ---
	*(void **)(&_x11_library.intern_atom) = dlsym(handle, "xcb_intern_atom");
	*(void **)(&_x11_library.intern_atom_reply) = dlsym(handle, "xcb_intern_atom_reply");

	// --- Window Management ---
	*(void **)(&_x11_library.generate_id) = dlsym(handle, "xcb_generate_id");
	*(void **)(&_x11_library.create_window) = dlsym(handle, "xcb_create_window");
	*(void **)(&_x11_library.map_window) = dlsym(handle, "xcb_map_window");
	*(void **)(&_x11_library.change_property) = dlsym(handle, "xcb_change_property");

	// --- Geometry ---
	*(void **)(&_x11_library.get_geometry) = dlsym(handle, "xcb_get_geometry");
	*(void **)(&_x11_library.get_geometry_reply) = dlsym(handle, "xcb_get_geometry_reply");

	// --- Events & Flushing ---
	*(void **)(&_x11_library.poll_for_event) = dlsym(handle, "xcb_poll_for_event");
	*(void **)(&_x11_library.flush) = dlsym(handle, "xcb_flush");

	*(void **)(&_x11_library.create_pixmap) = dlsym(handle, "xcb_create_pixmap");
	*(void **)(&_x11_library.create_cursor) = dlsym(handle, "xcb_create_cursor");
	*(void **)(&_x11_library.free_pixmap) = dlsym(handle, "xcb_free_pixmap");
	*(void **)(&_x11_library.free_cursor) = dlsym(handle, "xcb_free_cursor");

	*(void **)(&_x11_library.connection_has_error) = dlsym(handle, "xcb_connection_has_error");
	*(void **)(&_x11_library.warp_pointer) = dlsym(handle, "xcb_warp_pointer");
	*(void **)(&_x11_library.grab_pointer) = dlsym(handle, "xcb_grab_pointer");
	*(void **)(&_x11_library.ungrab_pointer) = dlsym(handle, "xcb_ungrab_pointer");

	// Verify all symbols loaded correctly
	// Assuming X11Library contains only pointers starting from the handle
	void **array = (void **)&_x11_library;
	size_t ptr_count = sizeof(X11Library) / sizeof(void *);

	for (size_t i = 0; i < ptr_count; i++) {
		if (array[i] == NULL) {
			dlclose(handle);
			_x11_library.handle = NULL;
			return false;
		}
	}

	// Assign Platform Internal VTable
	internal->startup = x11_startup;
	internal->shutdown = x11_shutdown;

	internal->poll_events = x11_poll_events;
	internal->should_close = x11_should_close;

	internal->logical_dimensions = x11_get_logical_dimensions;
	internal->physical_dimensions = x11_get_physical_dimensions;

	internal->pointer_mode = x11_pointer_mode;

	internal->time = x11_time;
	internal->time_ms = x11_time_ms;
	internal->random_64 = x11_random_64;

	internal->create_vulkan_surface = x11_create_vulkan_surface;
	internal->vulkan_extensions = x11_vulkan_extensions;

	return true;
}

bool x11_startup(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	x11->connection = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(x11->connection)) {
		LOG_ERROR("Failed to connect to X server");
		return false;
	}

	x11->window = xcb_generate_id(x11->connection);

	create_key_table(x11);

	// Access setup data
	const struct xcb_setup_t *setup = xcb_get_setup(x11->connection);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
	xcb_screen_t *screen = iter.data;
	// --- Create Hidden Cursor ---
	xcb_pixmap_t cursor_pixmap = xcb_generate_id(x11->connection);
	xcb_create_pixmap(x11->connection, 1, cursor_pixmap, screen->root, 1, 1);

	// Create the cursor using the blank pixmap for both source and mask
	// We use generic colors (0,0,0) - since the pixmap is empty, they won't render
	x11->hidden_cursor = xcb_generate_id(x11->connection);
	xcb_create_cursor(
		x11->connection,
		x11->hidden_cursor,
		cursor_pixmap,
		cursor_pixmap,
		0, 0, 0, // Foreground RGB
		0, 0, 0, // Background RGB
		0, 0 // Hotspot X, Y
	);

	// We can free the pixmap immediately after creating the cursor
	xcb_free_pixmap(x11->connection, cursor_pixmap);
	// Handle Atoms (Protocols)
	xcb_intern_atom_cookie_t proto_cookie = xcb_intern_atom(x11->connection, 1, 12, "WM_PROTOCOLS");
	xcb_intern_atom_cookie_t close_cookie = xcb_intern_atom(x11->connection, 0, 16, "WM_DELETE_WINDOW");

	xcb_intern_atom_reply_t *proto_reply = xcb_intern_atom_reply(x11->connection, proto_cookie, 0);
	xcb_intern_atom_reply_t *close_reply = xcb_intern_atom_reply(x11->connection, close_cookie, 0);

	// Safety check for atoms
	if (!proto_reply || !close_reply) {
		LOG_ERROR("Failed to intern atoms");
		return false;
	}

	x11->wm_protocols_atom = proto_reply->atom;
	x11->wm_delete_window_atom = close_reply->atom;

	free(proto_reply);
	free(close_reply);

	uint32_t event_mask =
		XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION |
		XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	uint32_t values[1] = {
		event_mask
	};

	xcb_create_window(
		x11->connection,
		XCB_COPY_FROM_PARENT,
		x11->window,
		screen->root,
		0, 0,
		platform->logical_width,
		platform->logical_height,
		1,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual,
		XCB_CW_EVENT_MASK,
		values);

	xcb_map_window(x11->connection, x11->window);

	xcb_flush(x11->connection);

	// Register WM_DELETE_WINDOW
	xcb_change_property(
		x11->connection,
		XCB_PROP_MODE_REPLACE,
		x11->window,
		x11->wm_protocols_atom,
		XCB_ATOM_ATOM,
		32,
		1,
		&x11->wm_delete_window_atom);

	return true;
}

void x11_shutdown(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	if (x11->connection) {
		xcb_disconnect(x11->connection);
	}
}

void x11_poll_events(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	xcb_generic_event_t *event = NULL;
	while ((event = xcb_poll_for_event(x11->connection))) {
		switch (event->response_type & ~0x80) {
			case XCB_CLIENT_MESSAGE: {
				xcb_client_message_event_t *cm = (xcb_client_message_event_t *)event;
				// Check if the message is WM_DELETE_WINDOW
				if (cm->data.data32[0] == x11->wm_delete_window_atom) {
					platform->should_close = true;
				}
			} break;

			case XCB_CONFIGURE_NOTIFY: {
				xcb_configure_notify_event_t *cfg = (xcb_configure_notify_event_t *)event;

				// Only trigger resize if dimensions actually changed
				if (cfg->width != platform->logical_width || cfg->height != platform->logical_height) {
					platform->logical_width = cfg->width;
					platform->logical_height = cfg->height;
					platform->physical_width = cfg->width; // X11 usually 1:1 unless using XRandR scaling logic
					platform->physical_height = cfg->height;

					WindowResizeEvent e = {
						.width = cfg->width,
						.height = cfg->height,

					};
					event_emit_struct(EVENT_PLATFORM_WINDOW_RESIZED, &e);
				}
			} break;

			case XCB_MOTION_NOTIFY: {
				xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;

				MouseMotionEvent e = { 0 };

				if (x11->pointer_mode == PLATFORM_POINTER_DISABLED) {
					// Locked Mode Logic (Fake raw input)
					int center_x = platform->logical_width / 2;
					int center_y = platform->logical_height / 2;

					// If we are at the center, this event was likely caused by our warp
					if (motion->event_x == center_x && motion->event_y == center_y) {
						break;
					}

					e.dx = (double)(motion->event_x - center_x);
					e.dy = (double)(motion->event_y - center_y);
					e.virtual_cursor = true;

					// X11 doesn't have a built-in virtual accumulator, so we just emit deltas
					// or you can track a virtual_x/y in your struct like Wayland does.

					// Warp back to center
					xcb_warp_pointer(x11->connection, XCB_NONE, x11->window, 0, 0, 0, 0, center_x, center_y);
					xcb_flush(x11->connection);
				} else {
					// Standard Mode
					e.x = (double)motion->event_x;
					e.y = (double)motion->event_y;
					// Note: Calculating DX/DY in standard mode requires storing last_mouse_x/y
				}

				event_emit_struct(EVENT_PLATFORM_MOUSE_MOTION, &e);
			} break;

			case XCB_BUTTON_PRESS:
			case XCB_BUTTON_RELEASE: {
				xcb_button_press_event_t *bp = (xcb_button_press_event_t *)event;
				bool pressed = (event->response_type & ~0x80) == XCB_BUTTON_PRESS;

				MouseButtonEvent e = { 0 };

				e.x = bp->event_x;
				e.y = bp->event_y;

				// Map X11 buttons (1=Left, 2=Middle, 3=Right, 4/5=Scroll)
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
						e.button = BTN_LEFT; // Fallback
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

				// X11 Keycodes are Linux Scancodes + 8
				uint32_t scancode_index = kp->detail - 8;

				if (scancode_index < 256) {
					e.key = x11->keycodes[scancode_index];
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

bool x11_should_close(Platform *platform) {
	return platform->should_close;
}

bool x11_pointer_mode(Platform *platform, PointerMode mode) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	if (mode == x11->pointer_mode)
		return false;

	x11->pointer_mode = mode;

	if (mode == PLATFORM_POINTER_DISABLED) {
		// Warp to center first to avoid huge jump
		int center_x = platform->logical_width / 2;
		int center_y = platform->logical_height / 2;

		xcb_warp_pointer(x11->connection, XCB_NONE, x11->window, 0, 0, 0, 0, center_x, center_y);

		// Grab pointer
		// Note: x11->hidden_cursor must be initialized somewhere (e.g. x11_startup)
		// using xcb_create_cursor + xcb_create_pixmap for a blank cursor.
		xcb_grab_pointer(
			x11->connection,
			1,
			x11->window,
			XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
			XCB_GRAB_MODE_ASYNC,
			XCB_GRAB_MODE_ASYNC,
			x11->window,
			x11->hidden_cursor,
			XCB_TIME_CURRENT_TIME);
	} else {
		xcb_ungrab_pointer(x11->connection, XCB_TIME_CURRENT_TIME);
	}

	xcb_flush(x11->connection);
	return true;
}

double x11_time(Platform *platform) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}

uint64_t x11_time_ms(Platform *platform) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

uint64_t x11_random_64(Platform *platform) {
	// Simple random fallback if sys/random not available, or use the one from WL if shared
	uint64_t r = 0;
	r = (uint64_t)rand();
	r = (r << 32) | rand();
	return r;
}

void x11_get_logical_dimensions(Platform *platform, uint32_t *width, uint32_t *height) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(
		x11->connection,
		xcb_get_geometry(x11->connection, x11->window),
		NULL);

	if (geometry) {
		*width = geometry->width;
		*height = geometry->height;
		free(geometry);
	}
}

void x11_get_physical_dimensions(Platform *platform, uint32_t *width, uint32_t *height) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	X11Platform *x11 = &internal->x11;

	// In X11 without DPI scaling logic, physical ~ logical often,
	// unless querying RANDR for actual mm sizes.
	xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(
		x11->connection,
		xcb_get_geometry(x11->connection, x11->window),
		NULL);

	if (geometry) {
		*width = geometry->width;
		*height = geometry->height;
		free(geometry);
	}
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

const char **x11_vulkan_extensions(uint32_t *count) {
	*count = sizeof(extensions) / sizeof(*extensions);
	return extensions;
}

void create_key_table(X11Platform *x11) {
	memset(x11->keycodes, -1, sizeof(x11->keycodes));

	x11->keycodes[KEY_GRAVE] = KEY_CODE_GRAVE;
	x11->keycodes[KEY_1] = KEY_CODE_1;
	x11->keycodes[KEY_2] = KEY_CODE_2;
	x11->keycodes[KEY_3] = KEY_CODE_3;
	x11->keycodes[KEY_4] = KEY_CODE_4;
	x11->keycodes[KEY_5] = KEY_CODE_5;
	x11->keycodes[KEY_6] = KEY_CODE_6;
	x11->keycodes[KEY_7] = KEY_CODE_7;
	x11->keycodes[KEY_8] = KEY_CODE_8;
	x11->keycodes[KEY_9] = KEY_CODE_9;
	x11->keycodes[KEY_0] = KEY_CODE_0;
	x11->keycodes[KEY_SPACE] = KEY_CODE_SPACE;
	x11->keycodes[KEY_MINUS] = KEY_CODE_MINUS;
	x11->keycodes[KEY_EQUAL] = KEY_CODE_EQUAL;
	x11->keycodes[KEY_Q] = KEY_CODE_Q;
	x11->keycodes[KEY_W] = KEY_CODE_W;
	x11->keycodes[KEY_E] = KEY_CODE_E;
	x11->keycodes[KEY_R] = KEY_CODE_R;
	x11->keycodes[KEY_T] = KEY_CODE_T;
	x11->keycodes[KEY_Y] = KEY_CODE_Y;
	x11->keycodes[KEY_U] = KEY_CODE_U;
	x11->keycodes[KEY_I] = KEY_CODE_I;
	x11->keycodes[KEY_O] = KEY_CODE_O;
	x11->keycodes[KEY_P] = KEY_CODE_P;
	x11->keycodes[KEY_LEFTBRACE] = KEY_CODE_LEFTBRACKET;
	x11->keycodes[KEY_RIGHTBRACE] = KEY_CODE_RIGHTBRACKET;
	x11->keycodes[KEY_A] = KEY_CODE_A;
	x11->keycodes[KEY_S] = KEY_CODE_S;
	x11->keycodes[KEY_D] = KEY_CODE_D;
	x11->keycodes[KEY_F] = KEY_CODE_F;
	x11->keycodes[KEY_G] = KEY_CODE_G;
	x11->keycodes[KEY_H] = KEY_CODE_H;
	x11->keycodes[KEY_J] = KEY_CODE_J;
	x11->keycodes[KEY_K] = KEY_CODE_K;
	x11->keycodes[KEY_L] = KEY_CODE_L;
	x11->keycodes[KEY_SEMICOLON] = KEY_CODE_SEMICOLON;
	x11->keycodes[KEY_APOSTROPHE] = KEY_CODE_APOSTROPHE;
	x11->keycodes[KEY_Z] = KEY_CODE_Z;
	x11->keycodes[KEY_X] = KEY_CODE_X;
	x11->keycodes[KEY_C] = KEY_CODE_C;
	x11->keycodes[KEY_V] = KEY_CODE_V;
	x11->keycodes[KEY_B] = KEY_CODE_B;
	x11->keycodes[KEY_N] = KEY_CODE_N;
	x11->keycodes[KEY_M] = KEY_CODE_M;
	x11->keycodes[KEY_COMMA] = KEY_CODE_COMMA;
	x11->keycodes[KEY_DOT] = KEY_CODE_PERIOD;
	x11->keycodes[KEY_SLASH] = KEY_CODE_SLASH;
	x11->keycodes[KEY_BACKSLASH] = KEY_CODE_BACKSLASH;
	x11->keycodes[KEY_ESC] = KEY_CODE_ESCAPE;
	x11->keycodes[KEY_TAB] = KEY_CODE_TAB;
	x11->keycodes[KEY_LEFTSHIFT] = KEY_CODE_LEFTSHIFT;
	x11->keycodes[KEY_RIGHTSHIFT] = KEY_CODE_RIGHTSHIFT;
	x11->keycodes[KEY_LEFTCTRL] = KEY_CODE_LEFTCTRL;
	x11->keycodes[KEY_RIGHTCTRL] = KEY_CODE_RIGHTCTRL;
	x11->keycodes[KEY_LEFTALT] = KEY_CODE_LEFTALT;
	x11->keycodes[KEY_RIGHTALT] = KEY_CODE_RIGHTALT;
	x11->keycodes[KEY_LEFTMETA] = KEY_CODE_LEFTMETA;
	x11->keycodes[KEY_RIGHTMETA] = KEY_CODE_RIGHTMETA;
	x11->keycodes[KEY_COMPOSE] = KEY_CODE_MENU;
	x11->keycodes[KEY_NUMLOCK] = KEY_CODE_NUMLOCK;
	x11->keycodes[KEY_CAPSLOCK] = KEY_CODE_CAPSLOCK;
	x11->keycodes[KEY_PRINT] = KEY_CODE_PRINT;
	x11->keycodes[KEY_SCROLLLOCK] = KEY_CODE_SCROLLLOCK;
	x11->keycodes[KEY_PAUSE] = KEY_CODE_PAUSE;
	x11->keycodes[KEY_DELETE] = KEY_CODE_DELETE;
	x11->keycodes[KEY_BACKSPACE] = KEY_CODE_BACKSPACE;
	x11->keycodes[KEY_ENTER] = KEY_CODE_ENTER;
	x11->keycodes[KEY_HOME] = KEY_CODE_HOME;
	x11->keycodes[KEY_END] = KEY_CODE_END;
	x11->keycodes[KEY_PAGEUP] = KEY_CODE_PAGEUP;
	x11->keycodes[KEY_PAGEDOWN] = KEY_CODE_PAGEDOWN;
	x11->keycodes[KEY_INSERT] = KEY_CODE_INSERT;
	x11->keycodes[KEY_LEFT] = KEY_CODE_LEFT;
	x11->keycodes[KEY_RIGHT] = KEY_CODE_RIGHT;
	x11->keycodes[KEY_DOWN] = KEY_CODE_DOWN;
	x11->keycodes[KEY_UP] = KEY_CODE_UP;
	x11->keycodes[KEY_F1] = KEY_CODE_F1;
	x11->keycodes[KEY_F2] = KEY_CODE_F2;
	x11->keycodes[KEY_F3] = KEY_CODE_F3;
	x11->keycodes[KEY_F4] = KEY_CODE_F4;
	x11->keycodes[KEY_F5] = KEY_CODE_F5;
	x11->keycodes[KEY_F6] = KEY_CODE_F6;
	x11->keycodes[KEY_F7] = KEY_CODE_F7;
	x11->keycodes[KEY_F8] = KEY_CODE_F8;
	x11->keycodes[KEY_F9] = KEY_CODE_F9;
	x11->keycodes[KEY_F10] = KEY_CODE_F10;
	x11->keycodes[KEY_F11] = KEY_CODE_F11;
	x11->keycodes[KEY_F12] = KEY_CODE_F12;
	x11->keycodes[KEY_F13] = KEY_CODE_F13;
	x11->keycodes[KEY_F14] = KEY_CODE_F14;
	x11->keycodes[KEY_F15] = KEY_CODE_F15;
	x11->keycodes[KEY_F16] = KEY_CODE_F16;
	x11->keycodes[KEY_F17] = KEY_CODE_F17;
	x11->keycodes[KEY_F18] = KEY_CODE_F18;
	x11->keycodes[KEY_F19] = KEY_CODE_F19;
	x11->keycodes[KEY_F20] = KEY_CODE_F20;
	x11->keycodes[KEY_F21] = KEY_CODE_F21;
	x11->keycodes[KEY_F22] = KEY_CODE_F22;
	x11->keycodes[KEY_F23] = KEY_CODE_F23;
	x11->keycodes[KEY_F24] = KEY_CODE_F24;
	x11->keycodes[KEY_KPSLASH] = KEY_CODE_KPSLASH;
	x11->keycodes[KEY_KPASTERISK] = KEY_CODE_KPASTERISK;
	x11->keycodes[KEY_KPMINUS] = KEY_CODE_KPMINUS;
	x11->keycodes[KEY_KPPLUS] = KEY_CODE_KPPLUS;
	x11->keycodes[KEY_KP0] = KEY_CODE_KP0;
	x11->keycodes[KEY_KP1] = KEY_CODE_KP1;
	x11->keycodes[KEY_KP2] = KEY_CODE_KP2;
	x11->keycodes[KEY_KP3] = KEY_CODE_KP3;
	x11->keycodes[KEY_KP4] = KEY_CODE_KP4;
	x11->keycodes[KEY_KP5] = KEY_CODE_KP5;
	x11->keycodes[KEY_KP6] = KEY_CODE_KP6;
	x11->keycodes[KEY_KP7] = KEY_CODE_KP7;
	x11->keycodes[KEY_KP8] = KEY_CODE_KP8;
	x11->keycodes[KEY_KP9] = KEY_CODE_KP9;
	x11->keycodes[KEY_KPDOT] = KEY_CODE_KPDOT;
	x11->keycodes[KEY_KPEQUAL] = KEY_CODE_KPEQUAL;
	x11->keycodes[KEY_KPENTER] = KEY_CODE_KPENTER;
	x11->keycodes[KEY_102ND] = KEY_CODE_WORLD_1;
}
