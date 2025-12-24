#pragma once

#include "platform.h"

#include "common.h"
#include "input/input_types.h"

#include <wayland-client-core.h>

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

typedef struct wl_display *(*PFN_wl_display_connect)(const char *);
typedef void (*PFN_wl_display_disconnect)(struct wl_display *);

typedef struct wl_proxy *(*PFN_wl_proxy_marshal_flags)(struct wl_proxy *, uint32_t, const struct wl_interface *, uint32_t, uint32_t, ...);
typedef uint32_t (*PFN_wl_proxy_get_version)(struct wl_proxy *);

typedef int (*PFN_wl_proxy_add_listener)(struct wl_proxy *, void (**)(void), void *);
typedef int (*PFN_wl_display_roundtrip)(struct wl_display *);

typedef int (*PFN_wl_display_dispatch)(struct wl_display *);

typedef struct xkb_context *(*PFN_xkb_context_new)(enum xkb_context_flags);
typedef void (*PFN_xkb_context_unref)(struct xkb_context *);

typedef struct xkb_keymap *(*PFN_xkb_keymap_new_from_string)(struct xkb_context *, const char *, enum xkb_keymap_format, enum xkb_keymap_compile_flags);
typedef void (*PFN_xkb_keymap_unref)(struct xkb_keymap *);
typedef xkb_mod_index_t (*PFN_xkb_keymap_mod_get_index)(struct xkb_keymap *, const char *);

typedef struct xkb_state *(*PFN_xkb_state_new)(struct xkb_keymap *);
typedef void (*PFN_xkb_state_unref)(struct xkb_state *);
typedef int (*PFN_xkb_state_mod_index_is_active)(struct xkb_state *, xkb_mod_index_t, enum xkb_state_component);

typedef xkb_keysym_t (*PFN_xkb_state_key_get_one_sym)(struct xkb_state *state, xkb_keycode_t key);
typedef int (*PFN_xkb_state_key_get_syms)(struct xkb_state *, xkb_keycode_t, const xkb_keysym_t **);
typedef uint32_t (*PFN_xkb_keysym_to_utf32)(xkb_keysym_t);
typedef int (*PFN_xkb_keysym_to_utf8)(xkb_keysym_t, char *, size_t);

typedef int (*PFN_xkb_keysym_get_name)(xkb_keysym_t keysym, char *buffer, size_t size);

typedef enum xkb_state_component (*PFN_xkb_state_update_mask)(struct xkb_state *, xkb_mod_mask_t, xkb_mod_mask_t, xkb_mod_mask_t, xkb_layout_index_t, xkb_layout_index_t, xkb_layout_index_t);

// typedef int (*PFN_xkb_keymap_key_repeats)(struct xkb_keymap *, xkb_keycode_t);
// typedef int (*PFN_xkb_keymap_key_get_syms_by_level)(struct xkb_keymap *, xkb_keycode_t, xkb_layout_index_t, xkb_level_index_t, const xkb_keysym_t **);

// typedef xkb_layout_index_t (*PFN_xkb_state_key_get_layout)(struct xkb_state *, xkb_keycode_t);

typedef struct wl_library {
	void *handle;

	PFN_wl_display_connect display_connect;
	PFN_wl_display_disconnect display_disconnect;

	PFN_wl_proxy_marshal_flags proxy_marshal_flags;
	PFN_wl_proxy_get_version proxy_get_version;

	PFN_wl_proxy_add_listener proxy_add_listener;
	PFN_wl_display_roundtrip display_roundtrip;

	PFN_wl_display_dispatch display_dispatch;

	struct {
		void *handle;

		PFN_xkb_context_new context_new;
		PFN_xkb_context_unref context_unref;

		PFN_xkb_keymap_new_from_string keymap_new_from_string;
		PFN_xkb_keymap_unref keymap_unref;
		PFN_xkb_keymap_mod_get_index keymap_mod_get_index;

		PFN_xkb_state_new state_new;
		PFN_xkb_state_unref state_unref;

		PFN_xkb_state_key_get_one_sym state_key_get_one_sym;
		PFN_xkb_state_key_get_syms state_key_get_syms;
		PFN_xkb_state_mod_index_is_active state_mod_index_is_active;

		PFN_xkb_keysym_to_utf32 keysym_to_utf32;
		PFN_xkb_keysym_to_utf8 keysym_to_utf8;

		PFN_xkb_keysym_get_name keysym_get_name;

		PFN_xkb_state_update_mask state_update_mask;
	} xkb;

} WLLibrary;
extern WLLibrary _wl_library;

#define wl_display_connect _wl_library.display_connect
#define wl_display_disconnect _wl_library.display_disconnect

#define wl_proxy_marshal_flags _wl_library.proxy_marshal_flags
#define wl_proxy_get_version _wl_library.proxy_get_version

#define wl_proxy_add_listener _wl_library.proxy_add_listener
#define wl_display_roundtrip _wl_library.display_roundtrip

#define wl_display_dispatch _wl_library.display_dispatch

#define xkb_context_new _wl_library.xkb.context_new
#define xkb_context_unref _wl_library.xkb.context_unref

#define xkb_keymap_new_from_string _wl_library.xkb.keymap_new_from_string
#define xkb_keymap_unref _wl_library.xkb.keymap_unref
#define xkb_keymap_mod_get_index _wl_library.xkb.keymap_mod_get_index

#define xkb_state_new _wl_library.xkb.state_new
#define xkb_state_unref _wl_library.xkb.state_unref
#define xkb_state_mod_index_is_active _wl_library.xkb.state_mod_index_is_active

#define xkb_state_key_get_one_sym _wl_library.xkb.state_key_get_one_sym
#define xkb_state_key_get_syms _wl_library.xkb.state_key_get_syms
#define xkb_keysym_to_utf8 _wl_library.xkb.keysym_to_utf8
#define xkb_keysym_to_utf32 _wl_library.xkb.keysym_to_utf32

#define xkb_keysym_get_name _wl_library.xkb.keysym_get_name

#define xkb_state_update_mask _wl_library.xkb.state_update_mask
// #define xkb_keymap_key_repeats _glfw.wl.xkb.keymap_key_repeats

// #define xkb_keymap_key_get_syms_by_level _glfw.wl.xkb.keymap_key_get_syms_by_level
// #define xkb_state_key_get_layout _glfw.wl.xkb.state_key_get_layout

struct platform;

enum pointer_event_mask {
	POINTER_EVENT_ENTER = 1 << 0,
	POINTER_EVENT_LEAVE = 1 << 1,
	POINTER_EVENT_MOTION = 1 << 2,
	POINTER_EVENT_BUTTON = 1 << 3,
	POINTER_EVENT_AXIS = 1 << 4,
	POINTER_EVENT_AXIS_SOURCE = 1 << 5,
	POINTER_EVENT_AXIS_STOP = 1 << 6,
	POINTER_EVENT_AXIS_DISCRETE = 1 << 7,
};
typedef uint32_t PointerEventMask;

typedef struct pointer_event {
	PointerEventMask event_mask;
	wl_fixed_t surface_x, surface_y;
	wl_fixed_t delta_surface_x, delta_surface_y;
	uint32_t button, state;
	uint32_t time;
	uint32_t serial;
	struct {
		bool valid;
		wl_fixed_t value;
		uint32_t discrete;
	} axes[2];
	uint32_t axis_source;
} PointerEvent;

typedef struct wl_platform {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_output *output;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_surface *surface;
	struct xdg_wm_base *wm_base;

	struct wp_viewporter *viewporter;
	struct wp_viewport *viewport;
	struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
	struct wp_fractional_scale_v1 *fractional_scale;
	struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
	float scale_factor;

	struct zwp_pointer_constraints_v1 *pointer_constraints;
	struct zwp_relative_pointer_manager_v1 *relative_pointer_manager;

	struct zwp_relative_pointer_v1 *relative_pointer;
	struct zwp_locked_pointer_v1 *locked_pointer;
	struct wp_cursor_shape_device_v1 *cursor_shape_device;

	struct {
		struct xdg_surface *surface;
		struct xdg_toplevel *toplevel;
	} xdg;

	struct {
		struct xkb_context *context;
		struct xkb_keymap *keymap;
		struct xkb_state *state;

		ModKeyFlags modifiers;
	} xkb;

	PointerEvent current_pointer_frame;

	int32_t key_repeat_rate;
	int32_t key_repeat_delay;

	int16_t keycodes[256];

	PointerMode pointer_mode;

	double virtual_pointer_x, virtual_pointer_y;
	double pointer_x, pointer_y;

	bool use_vulkan;
} WLPlatform;

#define PLATFORM_WAYLAND_LIBRARY_STATE WLPlatform wl;

bool platform_init_wayland(Platform *platform);

bool wl_startup(Platform *platform);
void wl_shutdown(Platform *platform);

void wl_poll_events(Platform *platform);
bool wl_should_close(Platform *platform);

void wl_get_logical_dimensions(Platform *platform, uint32_t *width, uint32_t *height);
void wl_get_physical_dimensions(Platform *platform, uint32_t *width, uint32_t *height);

bool wl_pointer_mode(Platform *platform, PointerMode mode);

double wl_time(Platform *platform);
uint64_t wl_time_ms(Platform *platform);
uint64_t wl_random_64(Platform *platform);

struct VkSurfaceKHR_T;
struct VkInstance_T;

bool wl_create_vulkan_surface(Platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface);
const char **wl_vulkan_extensions(uint32_t *count);
