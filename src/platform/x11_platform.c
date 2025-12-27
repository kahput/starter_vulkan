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

					WindowResizeEvent e = event_create(WindowResizeEvent, SV_EVENT_WINDOW_RESIZED);
					e.width = cfg->width;
					e.height = cfg->height;
					event_emit((Event *)&e);
				}
			} break;

			case XCB_MOTION_NOTIFY: {
				xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;

				MouseMotionEvent e = event_create(MouseMotionEvent, SV_EVENT_MOUSE_MOTION);

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

				event_emit((Event *)&e);
			} break;

			case XCB_BUTTON_PRESS:
			case XCB_BUTTON_RELEASE: {
				xcb_button_press_event_t *bp = (xcb_button_press_event_t *)event;
				bool pressed = (event->response_type & ~0x80) == XCB_BUTTON_PRESS;

				MouseButtonEvent e = event_create(MouseButtonEvent,
					pressed ? SV_EVENT_MOUSE_BUTTON_PRESSED : SV_EVENT_MOUSE_BUTTON_RELEASED);

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
					event_emit((Event *)&e);
				}
			} break;

			case XCB_KEY_PRESS:
			case XCB_KEY_RELEASE: {
				xcb_key_press_event_t *kp = (xcb_key_press_event_t *)event;
				bool pressed = (event->response_type & ~0x80) == XCB_KEY_PRESS;

				KeyEvent e = event_create(KeyEvent,
					pressed ? SV_EVENT_KEY_PRESSED : SV_EVENT_KEY_RELEASED);

				// X11 Keycodes are Linux Scancodes + 8
				uint32_t scancode_index = kp->detail - 8;

				if (scancode_index < 256) {
					e.key = x11->keycodes[scancode_index];
				} else {
					e.key = SV_KEY_UNKOWN;
				}

				// You can add modifier logic here by checking kp->state
				// e.mods = ...

				event_emit((Event *)&e);
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

	x11->keycodes[KEY_GRAVE] = SV_KEY_GRAVE;
	x11->keycodes[KEY_1] = SV_KEY_1;
	x11->keycodes[KEY_2] = SV_KEY_2;
	x11->keycodes[KEY_3] = SV_KEY_3;
	x11->keycodes[KEY_4] = SV_KEY_4;
	x11->keycodes[KEY_5] = SV_KEY_5;
	x11->keycodes[KEY_6] = SV_KEY_6;
	x11->keycodes[KEY_7] = SV_KEY_7;
	x11->keycodes[KEY_8] = SV_KEY_8;
	x11->keycodes[KEY_9] = SV_KEY_9;
	x11->keycodes[KEY_0] = SV_KEY_0;
	x11->keycodes[KEY_SPACE] = SV_KEY_SPACE;
	x11->keycodes[KEY_MINUS] = SV_KEY_MINUS;
	x11->keycodes[KEY_EQUAL] = SV_KEY_EQUAL;
	x11->keycodes[KEY_Q] = SV_KEY_Q;
	x11->keycodes[KEY_W] = SV_KEY_W;
	x11->keycodes[KEY_E] = SV_KEY_E;
	x11->keycodes[KEY_R] = SV_KEY_R;
	x11->keycodes[KEY_T] = SV_KEY_T;
	x11->keycodes[KEY_Y] = SV_KEY_Y;
	x11->keycodes[KEY_U] = SV_KEY_U;
	x11->keycodes[KEY_I] = SV_KEY_I;
	x11->keycodes[KEY_O] = SV_KEY_O;
	x11->keycodes[KEY_P] = SV_KEY_P;
	x11->keycodes[KEY_LEFTBRACE] = SV_KEY_LEFTBRACKET;
	x11->keycodes[KEY_RIGHTBRACE] = SV_KEY_RIGHTBRACKET;
	x11->keycodes[KEY_A] = SV_KEY_A;
	x11->keycodes[KEY_S] = SV_KEY_S;
	x11->keycodes[KEY_D] = SV_KEY_D;
	x11->keycodes[KEY_F] = SV_KEY_F;
	x11->keycodes[KEY_G] = SV_KEY_G;
	x11->keycodes[KEY_H] = SV_KEY_H;
	x11->keycodes[KEY_J] = SV_KEY_J;
	x11->keycodes[KEY_K] = SV_KEY_K;
	x11->keycodes[KEY_L] = SV_KEY_L;
	x11->keycodes[KEY_SEMICOLON] = SV_KEY_SEMICOLON;
	x11->keycodes[KEY_APOSTROPHE] = SV_KEY_APOSTROPHE;
	x11->keycodes[KEY_Z] = SV_KEY_Z;
	x11->keycodes[KEY_X] = SV_KEY_X;
	x11->keycodes[KEY_C] = SV_KEY_C;
	x11->keycodes[KEY_V] = SV_KEY_V;
	x11->keycodes[KEY_B] = SV_KEY_B;
	x11->keycodes[KEY_N] = SV_KEY_N;
	x11->keycodes[KEY_M] = SV_KEY_M;
	x11->keycodes[KEY_COMMA] = SV_KEY_COMMA;
	x11->keycodes[KEY_DOT] = SV_KEY_PERIOD;
	x11->keycodes[KEY_SLASH] = SV_KEY_SLASH;
	x11->keycodes[KEY_BACKSLASH] = SV_KEY_BACKSLASH;
	x11->keycodes[KEY_ESC] = SV_KEY_ESCAPE;
	x11->keycodes[KEY_TAB] = SV_KEY_TAB;
	x11->keycodes[KEY_LEFTSHIFT] = SV_KEY_LEFTSHIFT;
	x11->keycodes[KEY_RIGHTSHIFT] = SV_KEY_RIGHTSHIFT;
	x11->keycodes[KEY_LEFTCTRL] = SV_KEY_LEFTCTRL;
	x11->keycodes[KEY_RIGHTCTRL] = SV_KEY_RIGHTCTRL;
	x11->keycodes[KEY_LEFTALT] = SV_KEY_LEFTALT;
	x11->keycodes[KEY_RIGHTALT] = SV_KEY_RIGHTALT;
	x11->keycodes[KEY_LEFTMETA] = SV_KEY_LEFTMETA;
	x11->keycodes[KEY_RIGHTMETA] = SV_KEY_RIGHTMETA;
	x11->keycodes[KEY_COMPOSE] = SV_KEY_MENU;
	x11->keycodes[KEY_NUMLOCK] = SV_KEY_NUMLOCK;
	x11->keycodes[KEY_CAPSLOCK] = SV_KEY_CAPSLOCK;
	x11->keycodes[KEY_PRINT] = SV_KEY_PRINT;
	x11->keycodes[KEY_SCROLLLOCK] = SV_KEY_SCROLLLOCK;
	x11->keycodes[KEY_PAUSE] = SV_KEY_PAUSE;
	x11->keycodes[KEY_DELETE] = SV_KEY_DELETE;
	x11->keycodes[KEY_BACKSPACE] = SV_KEY_BACKSPACE;
	x11->keycodes[KEY_ENTER] = SV_KEY_ENTER;
	x11->keycodes[KEY_HOME] = SV_KEY_HOME;
	x11->keycodes[KEY_END] = SV_KEY_END;
	x11->keycodes[KEY_PAGEUP] = SV_KEY_PAGEUP;
	x11->keycodes[KEY_PAGEDOWN] = SV_KEY_PAGEDOWN;
	x11->keycodes[KEY_INSERT] = SV_KEY_INSERT;
	x11->keycodes[KEY_LEFT] = SV_KEY_LEFT;
	x11->keycodes[KEY_RIGHT] = SV_KEY_RIGHT;
	x11->keycodes[KEY_DOWN] = SV_KEY_DOWN;
	x11->keycodes[KEY_UP] = SV_KEY_UP;
	x11->keycodes[KEY_F1] = SV_KEY_F1;
	x11->keycodes[KEY_F2] = SV_KEY_F2;
	x11->keycodes[KEY_F3] = SV_KEY_F3;
	x11->keycodes[KEY_F4] = SV_KEY_F4;
	x11->keycodes[KEY_F5] = SV_KEY_F5;
	x11->keycodes[KEY_F6] = SV_KEY_F6;
	x11->keycodes[KEY_F7] = SV_KEY_F7;
	x11->keycodes[KEY_F8] = SV_KEY_F8;
	x11->keycodes[KEY_F9] = SV_KEY_F9;
	x11->keycodes[KEY_F10] = SV_KEY_F10;
	x11->keycodes[KEY_F11] = SV_KEY_F11;
	x11->keycodes[KEY_F12] = SV_KEY_F12;
	x11->keycodes[KEY_F13] = SV_KEY_F13;
	x11->keycodes[KEY_F14] = SV_KEY_F14;
	x11->keycodes[KEY_F15] = SV_KEY_F15;
	x11->keycodes[KEY_F16] = SV_KEY_F16;
	x11->keycodes[KEY_F17] = SV_KEY_F17;
	x11->keycodes[KEY_F18] = SV_KEY_F18;
	x11->keycodes[KEY_F19] = SV_KEY_F19;
	x11->keycodes[KEY_F20] = SV_KEY_F20;
	x11->keycodes[KEY_F21] = SV_KEY_F21;
	x11->keycodes[KEY_F22] = SV_KEY_F22;
	x11->keycodes[KEY_F23] = SV_KEY_F23;
	x11->keycodes[KEY_F24] = SV_KEY_F24;
	x11->keycodes[KEY_KPSLASH] = SV_KEY_KPSLASH;
	x11->keycodes[KEY_KPASTERISK] = SV_KEY_KPASTERISK;
	x11->keycodes[KEY_KPMINUS] = SV_KEY_KPMINUS;
	x11->keycodes[KEY_KPPLUS] = SV_KEY_KPPLUS;
	x11->keycodes[KEY_KP0] = SV_KEY_KP0;
	x11->keycodes[KEY_KP1] = SV_KEY_KP1;
	x11->keycodes[KEY_KP2] = SV_KEY_KP2;
	x11->keycodes[KEY_KP3] = SV_KEY_KP3;
	x11->keycodes[KEY_KP4] = SV_KEY_KP4;
	x11->keycodes[KEY_KP5] = SV_KEY_KP5;
	x11->keycodes[KEY_KP6] = SV_KEY_KP6;
	x11->keycodes[KEY_KP7] = SV_KEY_KP7;
	x11->keycodes[KEY_KP8] = SV_KEY_KP8;
	x11->keycodes[KEY_KP9] = SV_KEY_KP9;
	x11->keycodes[KEY_KPDOT] = SV_KEY_KPDOT;
	x11->keycodes[KEY_KPEQUAL] = SV_KEY_KPEQUAL;
	x11->keycodes[KEY_KPENTER] = SV_KEY_KPENTER;
	x11->keycodes[KEY_102ND] = SV_KEY_WORLD_1;
}
