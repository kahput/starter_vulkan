#pragma once

#include <xcb/randr.h>
#include <xcb/xcb.h>

#include "platform.h"
#include "common.h"

// --- Function Pointer Typedefs ---

typedef xcb_connection_t *(*PFN_xcb_connect)(const char *displayname, int *screenp);
typedef void (*PFN_xcb_disconnect)(xcb_connection_t *c);

typedef const struct xcb_setup_t *(*PFN_xcb_get_setup)(xcb_connection_t *c);
typedef xcb_screen_iterator_t (*PFN_xcb_setup_roots_iterator)(const xcb_setup_t *R);

typedef xcb_intern_atom_cookie_t (*PFN_xcb_intern_atom)(xcb_connection_t *c, uint8_t only_if_exists, uint16_t name_len, const char *name);
typedef xcb_intern_atom_reply_t *(*PFN_xcb_intern_atom_reply)(xcb_connection_t *c, xcb_intern_atom_cookie_t cookie, xcb_generic_error_t **e);

typedef uint32_t (*PFN_xcb_generate_id)(xcb_connection_t *c);
typedef xcb_void_cookie_t (*PFN_xcb_create_window)(xcb_connection_t *c, uint8_t depth, xcb_window_t wid, xcb_window_t parent, int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t border_width, uint16_t _class, xcb_visualid_t visual, uint32_t value_mask, const void *value_list);
typedef xcb_void_cookie_t (*PFN_xcb_map_window)(xcb_connection_t *c, xcb_window_t window);
typedef xcb_void_cookie_t (*PFN_xcb_change_property)(xcb_connection_t *c, uint8_t mode, xcb_window_t window, xcb_atom_t property, xcb_atom_t type, uint8_t format, uint32_t data_len, const void *data);

typedef int (*PFN_xcb_flush)(xcb_connection_t *c);
typedef xcb_generic_event_t *(*PFN_xcb_poll_for_event)(xcb_connection_t *c);

typedef xcb_get_geometry_cookie_t (*PFN_xcb_get_geometry)(xcb_connection_t *c, xcb_drawable_t drawable);
typedef xcb_get_geometry_reply_t *(*PFN_xcb_get_geometry_reply)(xcb_connection_t *c, xcb_get_geometry_cookie_t cookie, xcb_generic_error_t **e);

typedef xcb_void_cookie_t (*PFN_xcb_create_pixmap)(xcb_connection_t *, uint8_t, xcb_pixmap_t, xcb_drawable_t, uint16_t, uint16_t);
typedef xcb_void_cookie_t (*PFN_xcb_create_cursor)(xcb_connection_t *, xcb_cursor_t, xcb_pixmap_t, xcb_pixmap_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint32_t, uint32_t);
typedef xcb_void_cookie_t (*PFN_xcb_free_pixmap)(xcb_connection_t *, xcb_pixmap_t);
typedef xcb_void_cookie_t (*PFN_xcb_free_cursor)(xcb_connection_t *, xcb_cursor_t);

// Connection handling
typedef int (*PFN_xcb_connection_has_error)(xcb_connection_t *c);

// Pointer/Input handling
typedef xcb_void_cookie_t (*PFN_xcb_warp_pointer)(xcb_connection_t *c, xcb_window_t src_window, xcb_window_t dst_window, int16_t src_x, int16_t src_y, uint16_t src_width, uint16_t src_height, int16_t dst_x, int16_t dst_y);

typedef xcb_grab_pointer_cookie_t (*PFN_xcb_grab_pointer)(xcb_connection_t *c, uint8_t owner_events, xcb_window_t grab_window, uint16_t event_mask, uint8_t pointer_mode, uint8_t keyboard_mode, xcb_window_t confine_to, xcb_cursor_t cursor, xcb_timestamp_t time);

typedef xcb_void_cookie_t (*PFN_xcb_ungrab_pointer)(xcb_connection_t *c, xcb_timestamp_t time);

typedef struct x11_library {
	void *handle;

	PFN_xcb_connect connect;
	PFN_xcb_disconnect disconnect;

	PFN_xcb_get_setup get_setup;
	PFN_xcb_setup_roots_iterator setup_roots_iterator;

	PFN_xcb_intern_atom intern_atom;
	PFN_xcb_intern_atom_reply intern_atom_reply;

	PFN_xcb_generate_id generate_id;
	PFN_xcb_create_window create_window;
	PFN_xcb_map_window map_window;
	PFN_xcb_change_property change_property;

	PFN_xcb_get_geometry get_geometry;
	PFN_xcb_get_geometry_reply get_geometry_reply;

	PFN_xcb_poll_for_event poll_for_event;
	PFN_xcb_flush flush;

	PFN_xcb_create_pixmap create_pixmap;
	PFN_xcb_create_cursor create_cursor;
	PFN_xcb_free_pixmap free_pixmap;
	PFN_xcb_free_cursor free_cursor;

	PFN_xcb_connection_has_error connection_has_error;
	PFN_xcb_warp_pointer warp_pointer;
	PFN_xcb_grab_pointer grab_pointer;
	PFN_xcb_ungrab_pointer ungrab_pointer;
} X11Library;

extern X11Library _x11_library;

#define xcb_connect _x11_library.connect
#define xcb_disconnect _x11_library.disconnect

#define xcb_get_setup _x11_library.get_setup
#define xcb_setup_roots_iterator _x11_library.setup_roots_iterator

#define xcb_intern_atom _x11_library.intern_atom
#define xcb_intern_atom_reply _x11_library.intern_atom_reply

#define xcb_generate_id _x11_library.generate_id
#define xcb_create_window _x11_library.create_window
#define xcb_map_window _x11_library.map_window
#define xcb_change_property _x11_library.change_property

#define xcb_get_geometry _x11_library.get_geometry
#define xcb_get_geometry_reply _x11_library.get_geometry_reply

#define xcb_poll_for_event _x11_library.poll_for_event
#define xcb_flush _x11_library.flush

#define xcb_create_pixmap _x11_library.create_pixmap
#define xcb_create_cursor _x11_library.create_cursor
#define xcb_free_pixmap _x11_library.free_pixmap
#define xcb_free_cursor _x11_library.free_cursor

#define xcb_connection_has_error _x11_library.connection_has_error
#define xcb_warp_pointer _x11_library.warp_pointer
#define xcb_grab_pointer _x11_library.grab_pointer
#define xcb_ungrab_pointer _x11_library.ungrab_pointer

typedef struct x11_platform {
	xcb_connection_t *connection;
	xcb_window_t window;

	xcb_atom_t wm_delete_window_atom;
	xcb_atom_t wm_protocols_atom;

	xcb_cursor_t hidden_cursor;
	PointerMode pointer_mode;

	int32_t keycodes[256];
} X11Platform;

#define PLATFORM_X11_LIBRARY_STATE X11Platform x11;

bool platform_init_x11(Platform *platform);

bool x11_startup(Platform *platform);
void x11_shutdown(Platform *platform);

void x11_poll_events(Platform *platform);
bool x11_should_close(Platform *platform);

bool x11_pointer_mode(Platform *platform, PointerMode mode);

double x11_time(Platform *platform);
uint64_t x11_time_ms(Platform *platform);
uint64_t x11_random_64(Platform *platform);

void x11_get_logical_dimensions(Platform *platform, uint32_t *width, uint32_t *height);
void x11_get_physical_dimensions(Platform *platform, uint32_t *width, uint32_t *height);

struct VkSurfaceKHR_T;
struct VkInstance_T;

bool x11_create_vulkan_surface(Platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface);
const char **x11_vulkan_extensions(uint32_t *count);
