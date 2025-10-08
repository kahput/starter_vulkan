#pragma once

#include <xcb/randr.h>
#include <xcb/xcb.h>

#include <stdbool.h>
#include <stdint.h>

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

struct platform;

typedef struct x11_platform {
	xcb_connection_t *connection;
	xcb_window_t window;

	struct {
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
	} xcb;

	struct {
		fn_platform_dimensions logical_size;
		fn_platform_dimensions physical_size;
	} callback;

} X11Platform;

#define PLATFORM_X11_LIBRARY_STATE X11Platform x11;

bool platform_init_x11(struct platform *platform);

bool x11_startup(struct platform *platform);
void x11_shutdown(struct platform *platform);

void x11_poll_events(struct platform *platform);
bool x11_should_close(struct platform *platform);

uint64_t x11_time_ms(struct platform *platform);

void x11_get_logical_dimensions(struct platform *platform, uint32_t *width, uint32_t *height);
void x11_get_physical_dimensions(struct platform *platform, uint32_t *width, uint32_t *height);

void x11_set_logical_dimensions_callback(struct platform *platform, fn_platform_dimensions dimensions);
void x11_set_physical_dimensions_callback(struct platform *platform, fn_platform_dimensions dimensions);

struct VkSurfaceKHR_T;
struct VkInstance_T;

bool x11_create_vulkan_surface(struct platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface);
const char **x11_vulkan_extensions(struct platform *platform, uint32_t *count);
