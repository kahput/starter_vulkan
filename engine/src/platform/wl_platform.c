#include "platform/wl_platform.h"

#include "platform.h"
#include "platform/internal.h"

#include "common.h"
#include "core/logger.h"

#include "event.h"
#include "events/platform_events.h"

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>

#include "tablet-v2-client-protocol.h"

#include "cursor-shape-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include "tablet-v2-client-protocol-code.h"

#include "cursor-shape-v1-client-protocol-code.h"
#include "fractional-scale-v1-client-protocol-code.h"
#include "pointer-constraints-unstable-v1-client-protocol-code.h"
#include "relative-pointer-unstable-v1-client-protocol-code.h"
#include "viewporter-client-protocol-code.h"
#include "wayland-client-protocol-code.h"
#include "xdg-shell-client-protocol-code.h"

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>

#include <linux/input-event-codes.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <unistd.h>

#define LIBRARY_COUNT sizeof(struct wl_library) / sizeof(void *)
WLLibrary _wl_library = { 0 };

static const char *extensions[] = {
	VK_KHR_SURFACE_EXTENSION_NAME,
	VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
};

#define XKB_KEYCODE_OFFSET 8

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static const struct wl_registry_listener registry_listener = { .global = registry_handle_global, .global_remove = registry_handle_global_remove };

static void wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial);
static const struct xdg_wm_base_listener wm_base_listener = { .ping = wm_base_ping };

static void surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial);
static const struct xdg_surface_listener surface_listener = { .configure = surface_configure };

static void toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states);
static void toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel);
static void toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height);
static void toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities);
static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
	.configure_bounds = toplevel_configure_bounds,
	.wm_capabilities = toplevel_wm_capabilities,
};

static void preferred_scale(void *data, struct wp_fractional_scale_v1 *wp_fractional_scale_v1, uint32_t scale);
static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
	.preferred_scale = preferred_scale,
};

void seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities);
void seat_name(void *data, struct wl_seat *wl_seat, const char *name);
static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

void pointer_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y);
void pointer_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface);
void pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y);
void pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
void pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
void pointer_frame(void *data, struct wl_pointer *wl_pointer);
void pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source);
void pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis);
void pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete);
static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size);
void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys);
void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface);
void keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
void keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group);
void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay);
static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

static struct wl_buffer *create_shm_buffer(Platform *platform);
static void create_key_table(WLPlatform *platform);

bool platform_init_wayland(Platform *platform) {
	void *handle = dlopen("libwayland-client.so", RTLD_LAZY);
	if (handle == NULL)
		return false;

	struct platform_internal *internal = (struct platform_internal *)platform->internal;

	_wl_library.handle = handle;

	*(void **)(&_wl_library.display_connect) = dlsym(handle, "wl_display_connect");
	*(void **)(&_wl_library.display_disconnect) = dlsym(handle, "wl_display_disconnect");

	*(void **)(&_wl_library.proxy_marshal_flags) = dlsym(handle, "wl_proxy_marshal_flags");
	*(void **)(&_wl_library.proxy_get_version) = dlsym(handle, "wl_proxy_get_version");

	*(void **)(&_wl_library.proxy_add_listener) = dlsym(handle, "wl_proxy_add_listener");
	*(void **)(&_wl_library.display_roundtrip) = dlsym(handle, "wl_display_roundtrip");

	*(void **)(&_wl_library.display_dispatch) = dlsym(handle, "wl_display_dispatch");

	handle = dlopen("libxkbcommon.so", RTLD_LAZY);
	if (handle == NULL)
		return false;

	_wl_library.xkb.handle = handle;

	*(void **)(&_wl_library.xkb.context_new) = dlsym(handle, "xkb_context_new");
	*(void **)(&_wl_library.xkb.context_unref) = dlsym(handle, "xkb_context_unref");

	*(void **)(&_wl_library.xkb.keymap_new_from_string) = dlsym(handle, "xkb_keymap_new_from_string");
	*(void **)(&_wl_library.xkb.keymap_unref) = dlsym(handle, "xkb_keymap_unref");
	*(void **)(&_wl_library.xkb.keymap_mod_get_index) = dlsym(handle, "xkb_keymap_mod_get_index");

	*(void **)(&_wl_library.xkb.state_new) = dlsym(handle, "xkb_state_new");
	*(void **)(&_wl_library.xkb.state_unref) = dlsym(handle, "xkb_state_unref");
	*(void **)(&_wl_library.xkb.state_mod_index_is_active) = dlsym(handle, "xkb_state_mod_index_is_active");

	*(void **)(&_wl_library.xkb.state_key_get_one_sym) = dlsym(handle, "xkb_state_key_get_one_sym");
	*(void **)(&_wl_library.xkb.state_key_get_syms) = dlsym(handle, "xkb_state_key_get_syms");
	*(void **)(&_wl_library.xkb.keysym_to_utf32) = dlsym(handle, "xkb_keysym_to_utf32");
	*(void **)(&_wl_library.xkb.keysym_to_utf8) = dlsym(handle, "xkb_keysym_to_utf8");

	*(void **)(&_wl_library.xkb.keysym_get_name) = dlsym(handle, "xkb_keysym_get_name");

	*(void **)(&_wl_library.xkb.state_update_mask) = dlsym(handle, "xkb_state_update_mask");

	void **array = &_wl_library.handle;
	for (uint32_t i = 0; i < LIBRARY_COUNT; i++) {
		if (array[i] == NULL) {
			return false;
		}
	}

	internal->startup = wl_startup;
	internal->shutdown = wl_shutdown;

	internal->poll_events = wl_poll_events;
	internal->should_close = wl_should_close;

	internal->logical_dimensions = wl_get_logical_dimensions;
	internal->physical_dimensions = wl_get_physical_dimensions;

	internal->pointer_mode = wl_pointer_mode;

	internal->time = wl_time;
	internal->time_ms = wl_time_ms;
	internal->random_64 = wl_random_64;

	internal->create_vulkan_surface = wl_create_vulkan_surface;
	internal->vulkan_extensions = wl_vulkan_extensions;

	return true;
}

bool wl_startup(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	WLPlatform *wl = &internal->wl;

	wl->display = wl_display_connect(NULL);

	struct wl_display *display = wl->display;
	if (display == NULL) {
		LOG_ERROR("Failed to create wayland display");
		return false;
	}

	wl->registry = wl_display_get_registry(wl->display);

	if (wl->registry == NULL) {
		LOG_ERROR("Failed to create registry");
		return false;
	}

	wl_registry_add_listener(wl->registry, &registry_listener, wl);
	wl_display_roundtrip(wl->display);

	create_key_table(wl);

	wl->surface = wl_compositor_create_surface(wl->compositor);
	wl->xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	if (wl->surface == NULL) {
		LOG_ERROR("Failed to create surface");
		return false;
	}

	wl->viewport = wp_viewporter_get_viewport(wl->viewporter, wl->surface);
	wl->xdg.surface = xdg_wm_base_get_xdg_surface(wl->wm_base, wl->surface);
	xdg_surface_add_listener(wl->xdg.surface, &surface_listener, platform);

	wl->xdg.toplevel = xdg_surface_get_toplevel(wl->xdg.surface);
	xdg_toplevel_set_app_id(wl->xdg.toplevel, "vulkan-debug");

	// xdg_toplevel_set_fullscreen(wl->xdg.toplevel, wl->output);
	xdg_surface_set_window_geometry(wl->xdg.surface, 0, 0, platform->physical_width, platform->physical_height);
	// xdg_toplevel_set_min_size(wl->xdg.toplevel, platform->logical_width, platform->logical_height);
	// xdg_toplevel_set_max_size(wl->xdg.toplevel, platform->logical_width, platform->logical_height);
	//
	xdg_toplevel_add_listener(wl->xdg.toplevel, &toplevel_listener, platform);

	wl->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(wl->fractional_scale_manager, wl->surface);
	wp_fractional_scale_v1_add_listener(wl->fractional_scale, &fractional_scale_listener, platform);

	wl_seat_add_listener(wl->seat, &seat_listener, platform);

	wl_surface_commit(wl->surface);

	// Attach buffer in configure callback
	wl_display_roundtrip(wl->display);

	// Retrived surface fractional scaling from attached buffer
	wl_display_roundtrip(wl->display);

	return true;
}

void wl_shutdown(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	WLPlatform *wl = &internal->wl;

	wl_display_disconnect(wl->display);
}

void wl_poll_events(Platform *platform) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	WLPlatform *wl = &internal->wl;

	wl_display_roundtrip(wl->display);
}
bool wl_should_close(Platform *platform) {
	return platform->should_close;
}

void wl_get_logical_dimensions(Platform *platform, uint32_t *width, uint32_t *height) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	WLPlatform *wl = &internal->wl;

	*width = platform->logical_width;
	*height = platform->logical_height;
}
void wl_get_physical_dimensions(Platform *platform, uint32_t *width, uint32_t *height) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	WLPlatform *wl = &internal->wl;

	*width = platform->physical_width;
	*height = platform->physical_height;
}

void relative_pointer_relative_motion(void *data, struct zwp_relative_pointer_v1 *zwp_relative_pointer_v1, uint32_t utime_hi, uint32_t utime_lo, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel);
static const struct zwp_relative_pointer_v1_listener relative_pointer_listener = {
	.relative_motion = relative_pointer_relative_motion
};

bool wl_pointer_mode(Platform *platform, PointerMode mode) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	WLPlatform *wl = &internal->wl;

	PointerMode previous = wl->pointer_mode;
	wl->pointer_mode = mode;

	if (mode == PLATFORM_POINTER_DISABLED && previous != PLATFORM_POINTER_DISABLED) {
		wl->cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(wl->cursor_shape_manager, wl->pointer);
		wl_pointer_set_cursor(wl->pointer, wl->current_pointer_frame.serial, NULL, 0, 0);

		wl->virtual_pointer_x = platform->physical_width / 2.f;
		wl->virtual_pointer_y = platform->physical_height / 2.f;

		wl->relative_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(wl->relative_pointer_manager, wl->pointer);
		zwp_relative_pointer_v1_add_listener(wl->relative_pointer, &relative_pointer_listener, wl);

		wl->locked_pointer = zwp_pointer_constraints_v1_lock_pointer(wl->pointer_constraints, wl->surface, wl->pointer, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
		zwp_locked_pointer_v1_set_cursor_position_hint(wl->locked_pointer, wl_fixed_from_double(wl->virtual_pointer_x), wl_fixed_from_double(wl->virtual_pointer_y));
		return true;
	}
	if (mode != PLATFORM_POINTER_DISABLED && previous == PLATFORM_POINTER_DISABLED) {
		wp_cursor_shape_device_v1_set_shape(wl->cursor_shape_device, wl->current_pointer_frame.serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
		zwp_relative_pointer_v1_destroy(wl->relative_pointer);
		wl->relative_pointer = NULL;

		zwp_locked_pointer_v1_destroy(wl->locked_pointer);
		wl->locked_pointer = NULL;
		return true;
	}
	return false;
}

double wl_time(Platform *platform) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}

uint64_t wl_time_ms(Platform *platform) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

uint64_t wl_random_64(Platform *platform) {
	uint64_t random_value;
	if (getrandom(&random_value, sizeof(random_value), GRND_RANDOM) != sizeof(random_value))
		LOG_WARN("Platform: Random number failed");
	return random_value;
}

bool wl_create_vulkan_surface(Platform *platform, VkInstance instance, VkSurfaceKHR *surface) {
	struct platform_internal *internal = (struct platform_internal *)platform->internal;
	WLPlatform *wl = &internal->wl;

	wl->use_vulkan = true;

	LOG_INFO("Using Vulkan for presentation");

	VkWaylandSurfaceCreateInfoKHR surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
		.display = wl->display,
		.surface = wl->surface,
	};

	if (vkCreateWaylandSurfaceKHR(instance, &surface_create_info, NULL, surface) != VK_SUCCESS)
		return false;

	return true;
}

const char **wl_vulkan_extensions(uint32_t *count) {
	*count = sizeof(extensions) / sizeof(*extensions);
	return extensions;
}

void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t _version) {
	WLPlatform *wl = (WLPlatform *)data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		wl->compositor = wl_registry_bind(wl->registry, name, &wl_compositor_interface, 6);
	else if (strcmp(interface, wl_output_interface.name) == 0)
		wl->output = wl_registry_bind(wl->registry, name, &wl_output_interface, 2);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		wl->shm = wl_registry_bind(wl->registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		wl->wm_base = wl_registry_bind(wl->registry, name, &xdg_wm_base_interface, 7);
		xdg_wm_base_add_listener(wl->wm_base, &wm_base_listener, wl);
	} else if (strcmp(interface, wp_viewporter_interface.name) == 0)
		wl->viewporter = wl_registry_bind(wl->registry, name, &wp_viewporter_interface, 1);
	else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0)
		wl->fractional_scale_manager = wl_registry_bind(wl->registry, name, &wp_fractional_scale_manager_v1_interface, 1);
	else if (strcmp(interface, wl_seat_interface.name) == 0)
		wl->seat = wl_registry_bind(wl->registry, name, &wl_seat_interface, 7);
	else if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0)
		wl->pointer_constraints = wl_registry_bind(wl->registry, name, &zwp_pointer_constraints_v1_interface, _version);
	else if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0)
		wl->relative_pointer_manager = wl_registry_bind(wl->registry, name, &zwp_relative_pointer_manager_v1_interface, _version);
	else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0)
		wl->cursor_shape_manager = wl_registry_bind(wl->registry, name, &wp_cursor_shape_manager_v1_interface, _version);
}
void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

void surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	xdg_surface_ack_configure(wl->xdg.surface, serial);

	if (wl->use_vulkan == false) {
		wl_surface_attach(wl->surface, create_shm_buffer(platform), 0, 0);
		wl_surface_commit(wl->surface);
	}
}
void wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) { xdg_wm_base_pong(xdg_wm_base, serial); }

void toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	if ((width && height) && ((uint32_t)width != platform->logical_width || (uint32_t)height != platform->logical_height)) {
		platform->logical_width = width, platform->logical_height = height;
		platform->physical_width = platform->logical_width * wl->scale_factor, platform->physical_height = platform->logical_height * wl->scale_factor;
		wp_viewport_set_destination(wl->viewport, platform->logical_width, platform->logical_height);

		WindowResizeEvent event = { 0 };
		event.width = platform->physical_width, event.height = platform->physical_height;

		event_emit_struct(EVENT_PLATFORM_WINDOW_RESIZED, &event);
	}
}
void toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
	Platform *platform = (Platform *)data;
	platform->should_close = true;
}
void toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height) {}
void toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities) {}

void preferred_scale(void *data, struct wp_fractional_scale_v1 *wp_fractional_scale_v1, uint32_t scale) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	wl->scale_factor = (float)scale / 120.f;
}

void seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	bool have_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
	if (have_pointer && wl->pointer == NULL) {
		wl->pointer = wl_seat_get_pointer(wl->seat);
		wl_pointer_add_listener(wl->pointer, &pointer_listener, platform);
	} else if (have_pointer == false && wl->pointer != NULL) {
		wl_pointer_release(wl->pointer);
		wl->pointer = NULL;
	}

	bool have_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
	if (have_keyboard && wl->keyboard == NULL) {
		wl->keyboard = wl_seat_get_keyboard(wl->seat);
		wl_keyboard_add_listener(wl->keyboard, &keyboard_listener, platform);
	} else if (have_keyboard == false && wl->keyboard != NULL) {
		wl_keyboard_release(wl->keyboard);
		wl->keyboard = NULL;
	}
}
void seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	LOG_TRACE("Seat name: %s", name);
}

void pointer_frame(void *data, struct wl_pointer *wl_pointer) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	struct pointer_event *pointer_event = &wl->current_pointer_frame;

	// LOG_TRACE("pointer frame @ %d: ", event->time);
	if (pointer_event->event_mask & POINTER_EVENT_ENTER) {
		// LOG_TRACE("entered %d, %d ",
		// 	wl_fixed_to_int(pointer_event->surface_x),
		// 	wl_fixed_to_int(pointer_event->surface_y));
	}
	if (pointer_event->event_mask & POINTER_EVENT_LEAVE) {
		// LOG_TRACE("leave");
	}
	if (pointer_event->event_mask & POINTER_EVENT_MOTION) {
		MouseMotionEvent event = { 0 };
		event.x = wl_fixed_to_double(pointer_event->surface_x);
		event.y = wl_fixed_to_double(pointer_event->surface_y);
		event.dx = wl_fixed_to_double(pointer_event->delta_surface_x);
		event.dy = wl_fixed_to_double(pointer_event->delta_surface_y);

		event_emit_struct(EVENT_PLATFORM_MOUSE_MOTION, &event);
		// LOG_TRACE("motion %d, %d ",
		// 	wl_fixed_to_int(pointer_event->surface_x),
		// 	wl_fixed_to_int(pointer_event->surface_y));
	}
	if (pointer_event->event_mask & POINTER_EVENT_BUTTON) {
		char *state = pointer_event->state == WL_POINTER_BUTTON_STATE_RELEASED ? "released" : "pressed";

		MouseButtonEvent event = { 0 };
		event.mods = wl->xkb.modifiers;
		event.button = pointer_event->button - BTN_LEFT;
		event.x = wl_fixed_to_int(pointer_event->surface_x);
		event.y = wl_fixed_to_int(pointer_event->surface_y);

		event_emit_struct(pointer_event->state == WL_POINTER_BUTTON_STATE_PRESSED ? EVENT_PLATFORM_MOUSE_BUTTON_PRESSED : EVENT_PLATFORM_MOUSE_BUTTON_RELEASED, &event);
		// LOG_TRACE("button %d %s ", pointer_event->button, state);
	}
	uint32_t axis_events = POINTER_EVENT_AXIS | POINTER_EVENT_AXIS_SOURCE | POINTER_EVENT_AXIS_STOP | POINTER_EVENT_AXIS_DISCRETE;
	char *axis_name[2] = {
		[WL_POINTER_AXIS_VERTICAL_SCROLL] = "vertical",
		[WL_POINTER_AXIS_HORIZONTAL_SCROLL] = "horizontal",

	};
	char *axis_source[4] = {
		[WL_POINTER_AXIS_SOURCE_WHEEL] = "wheel",
		[WL_POINTER_AXIS_SOURCE_FINGER] = "finger",
		[WL_POINTER_AXIS_SOURCE_CONTINUOUS] = "continuous",
		[WL_POINTER_AXIS_SOURCE_WHEEL_TILT] = "wheel tilt",

	};
	if (pointer_event->event_mask & axis_events) {
		for (size_t i = 0; i < 2; ++i) {
			if (!pointer_event->axes[i].valid) {
				continue;
			}
			LOG_TRACE("%s axis ", axis_name[i]);
			if (pointer_event->event_mask & POINTER_EVENT_AXIS) {
				LOG_TRACE("value %.2f ", wl_fixed_to_double(pointer_event->axes[i].value));
			}
			if (pointer_event->event_mask & POINTER_EVENT_AXIS_DISCRETE) {
				LOG_TRACE("discrete %d ",
					pointer_event->axes[i].discrete);
			}
			if (pointer_event->event_mask & POINTER_EVENT_AXIS_SOURCE) {
				LOG_TRACE("via %s ",
					axis_source[pointer_event->axis_source]);
			}
			if (pointer_event->event_mask & POINTER_EVENT_AXIS_STOP) {
				LOG_TRACE("(stopped) ");
			}
		}
	}
	memset(pointer_event, 0, sizeof(*pointer_event));
}

void pointer_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	wl->current_pointer_frame.event_mask |= POINTER_EVENT_ENTER;
	wl->current_pointer_frame.serial = serial;
	wl->current_pointer_frame.surface_x = surface_x;
	wl->current_pointer_frame.surface_y = surface_y;
}
void pointer_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	wl->current_pointer_frame.event_mask |= POINTER_EVENT_LEAVE;
	wl->current_pointer_frame.serial = serial;
}
void pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	wl->current_pointer_frame.event_mask |= POINTER_EVENT_MOTION;
	wl->current_pointer_frame.time = time;

	wl->current_pointer_frame.delta_surface_x = wl->current_pointer_frame.surface_x - surface_x;
	wl->current_pointer_frame.delta_surface_y = wl->current_pointer_frame.surface_y - surface_y;

	wl->current_pointer_frame.surface_x = surface_x;
	wl->current_pointer_frame.surface_y = surface_y;
}
void pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	wl->current_pointer_frame.event_mask |= POINTER_EVENT_BUTTON;
	wl->current_pointer_frame.serial = serial;
	wl->current_pointer_frame.time = time;
	wl->current_pointer_frame.button = button;
	wl->current_pointer_frame.state = state;
}

void pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	wl->current_pointer_frame.event_mask |= POINTER_EVENT_AXIS;
	wl->current_pointer_frame.time = time;
	wl->current_pointer_frame.axes[axis].valid = true;
	wl->current_pointer_frame.axes[axis].value = value;
}
void pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	wl->current_pointer_frame.event_mask |= POINTER_EVENT_AXIS_SOURCE;
	wl->current_pointer_frame.axis_source = axis_source;
}
void pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	wl->current_pointer_frame.event_mask |= POINTER_EVENT_AXIS_STOP;
	wl->current_pointer_frame.time = time;
	wl->current_pointer_frame.axes[axis].valid = true;
}
void pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	wl->current_pointer_frame.event_mask |= POINTER_EVENT_AXIS_DISCRETE;
	wl->current_pointer_frame.axes[axis].valid = true;
	wl->current_pointer_frame.axes[axis].discrete = discrete;
}

void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}

	char *map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (map_shm == MAP_FAILED) {
		close(fd);
		return;
	}

	struct xkb_keymap *keymap = xkb_keymap_new_from_string(wl->xkb.context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map_shm, size);
	close(fd);

	struct xkb_state *state = xkb_state_new(keymap);

	xkb_keymap_unref(wl->xkb.keymap);
	xkb_state_unref(wl->xkb.state);
	wl->xkb.keymap = keymap;
	wl->xkb.state = state;
}

void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *scancodes) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	uint32_t *scancode;
	wl_array_for_each(scancode, scancodes) {
		char buf[128];

		const xkb_keycode_t keycode = *scancode + XKB_KEYCODE_OFFSET;

		const xkb_keysym_t *array;
		const xkb_keysym_t key_symbol = xkb_state_key_get_one_sym(wl->xkb.state, keycode);

		xkb_keysym_get_name(key_symbol, buf, sizeof(buf));
		// LOG_TRACE("sym: %-12s (%d), ", buf, key_symbol);

		xkb_keysym_to_utf8(key_symbol, buf, sizeof(buf));
		// LOG_TRACE("utf8: '%s'", buf);
	}
}
void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	KeyEvent event = { 0 };
	event.leave = true;

	event_emit_struct(EVENT_PLATFORM_KEY_RELEASED, &event);
}
void keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t scancode, uint32_t state) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	char name_buffer[128], utf8_buffer[128];

	const xkb_keycode_t keycode = scancode + XKB_KEYCODE_OFFSET;
	const char *action = state == WL_KEYBOARD_KEY_STATE_PRESSED ? "pressed" : "released";

	KeyEvent event = { 0 };
	event.key = scancode < countof(wl->keycodes) ? wl->keycodes[scancode] : KEY_CODE_UNKOWN;
	event.mods = wl->xkb.modifiers;
	// event.is_repeat = false;

	event_emit_struct(state == WL_KEYBOARD_KEY_STATE_PRESSED ? EVENT_PLATFORM_KEY_PRESSED : EVENT_PLATFORM_KEY_RELEASED, &event);

	const xkb_keysym_t key_symbol = xkb_state_key_get_one_sym(wl->xkb.state, keycode);

	xkb_keysym_get_name(key_symbol, name_buffer, sizeof(name_buffer));
	xkb_keysym_to_utf8(key_symbol, utf8_buffer, sizeof(utf8_buffer));

	// LOG_TRACE("Scancode: %d, Symbol: %-12s (%d), utf8: '%s'", scancode, name_buffer, key_symbol, utf8_buffer);
	// LOG_TRACE("Key %-8s", action);
}
void keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	xkb_state_update_mask(wl->xkb.state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
	wl->xkb.modifiers = 0;

	struct {
		xkb_mod_index_t index;
		unsigned int bit;
	} modifiers[] = {
		{ xkb_keymap_mod_get_index(wl->xkb.keymap, "Control"), MOD_KEY_CONTROL },
		{ xkb_keymap_mod_get_index(wl->xkb.keymap, "Mod1"), MOD_KEY_ALT },
		{ xkb_keymap_mod_get_index(wl->xkb.keymap, "Shift"), MOD_KEY_SHIFT },
		{ xkb_keymap_mod_get_index(wl->xkb.keymap, "Mod4"), MOD_KEY_SUPER },
		{ xkb_keymap_mod_get_index(wl->xkb.keymap, "Lock"), MOD_KEY_CAPSLOCK },
		{ xkb_keymap_mod_get_index(wl->xkb.keymap, "Mod2"), MOD_KEY_NUMLOCK }
	};

	for (size_t index = 0; index < countof(modifiers); ++index) {
		if (xkb_state_mod_index_is_active(wl->xkb.state,
				modifiers[index].index,
				XKB_STATE_MODS_EFFECTIVE) == 1) {
			wl->xkb.modifiers |= modifiers[index].bit;
		}
	}
}
void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) {
	Platform *platform = (Platform *)data;
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	wl->key_repeat_rate = rate;
	wl->key_repeat_delay = delay;
}

static int create_anonymous_file(off_t size) {
	char template[] = "/tmp/wayland-shm-XXXXXX";
	int fd = mkstemp(template);
	if (fd < 0)
		return -1;

	unlink(template); // we don’t need the file on disk
	if (ftruncate(fd, size) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

void relative_pointer_relative_motion(void *data, struct zwp_relative_pointer_v1 *zwp_relative_pointer_v1, uint32_t utime_hi, uint32_t utime_lo, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel) {
	WLPlatform *wl = (WLPlatform *)data;
	if (wl->pointer_mode != PLATFORM_POINTER_DISABLED)
		return;
	wl->virtual_pointer_x += wl_fixed_to_double(dx_unaccel);
	wl->virtual_pointer_y += wl_fixed_to_double(dy_unaccel);

	MouseMotionEvent event = { 0 };

	event.x = wl->virtual_pointer_x;
	event.y = wl->virtual_pointer_y;
	event.virtual_cursor = true;

	event_emit_struct(EVENT_PLATFORM_MOUSE_MOTION, &event);

	// LOG_INFO("Relative motion: { dx = %.2f, dy = %.2f }", wl_fixed_to_double(dx), wl_fixed_to_double(dy));
}

struct wl_buffer *create_shm_buffer(Platform *platform) {
	WLPlatform *wl = &((struct platform_internal *)platform->internal)->wl;

	const int stride = platform->logical_width * 4;
	const int length = platform->logical_width * platform->logical_height * 4;

	const int fd = create_anonymous_file(length);
	if (fd < 0) {
		LOG_ERROR("Wayland: Failed to create buffer file of size %d: %s", strerror(errno));
		return NULL;
	}

	void *data = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		LOG_ERROR("Wayland: Failed to map file: %s", strerror(errno));
		close(fd);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(wl->shm, fd, length);

	close(fd);

	unsigned char *target = data;

	/* Draw checkerboxed background */
	for (uint32_t i = 0; i < platform->logical_width * platform->logical_height; i++) {
		*target++ = (unsigned char)0;
		*target++ = (unsigned char)0;
		*target++ = (unsigned char)0;
		*target++ = (unsigned char)255;
	}

	struct wl_buffer *buffer =
		wl_shm_pool_create_buffer(pool, 0,
			platform->logical_width,
			platform->logical_height,
			stride, WL_SHM_FORMAT_ARGB8888);
	munmap(data, length);
	wl_shm_pool_destroy(pool);

	return buffer;
}

void create_key_table(WLPlatform *wl) {
	memset(wl->keycodes, -1, sizeof(wl->keycodes));

	wl->keycodes[KEY_GRAVE] = KEY_CODE_GRAVE;
	wl->keycodes[KEY_1] = KEY_CODE_1;
	wl->keycodes[KEY_2] = KEY_CODE_2;
	wl->keycodes[KEY_3] = KEY_CODE_3;
	wl->keycodes[KEY_4] = KEY_CODE_4;
	wl->keycodes[KEY_5] = KEY_CODE_5;
	wl->keycodes[KEY_6] = KEY_CODE_6;
	wl->keycodes[KEY_7] = KEY_CODE_7;
	wl->keycodes[KEY_8] = KEY_CODE_8;
	wl->keycodes[KEY_9] = KEY_CODE_9;
	wl->keycodes[KEY_0] = KEY_CODE_0;
	wl->keycodes[KEY_SPACE] = KEY_CODE_SPACE;
	wl->keycodes[KEY_MINUS] = KEY_CODE_MINUS;
	wl->keycodes[KEY_EQUAL] = KEY_CODE_EQUAL;
	wl->keycodes[KEY_Q] = KEY_CODE_Q;
	wl->keycodes[KEY_W] = KEY_CODE_W;
	wl->keycodes[KEY_E] = KEY_CODE_E;
	wl->keycodes[KEY_R] = KEY_CODE_R;
	wl->keycodes[KEY_T] = KEY_CODE_T;
	wl->keycodes[KEY_Y] = KEY_CODE_Y;
	wl->keycodes[KEY_U] = KEY_CODE_U;
	wl->keycodes[KEY_I] = KEY_CODE_I;
	wl->keycodes[KEY_O] = KEY_CODE_O;
	wl->keycodes[KEY_P] = KEY_CODE_P;
	wl->keycodes[KEY_LEFTBRACE] = KEY_CODE_LEFTBRACKET;
	wl->keycodes[KEY_RIGHTBRACE] = KEY_CODE_RIGHTBRACKET;
	wl->keycodes[KEY_A] = KEY_CODE_A;
	wl->keycodes[KEY_S] = KEY_CODE_S;
	wl->keycodes[KEY_D] = KEY_CODE_D;
	wl->keycodes[KEY_F] = KEY_CODE_F;
	wl->keycodes[KEY_G] = KEY_CODE_G;
	wl->keycodes[KEY_H] = KEY_CODE_H;
	wl->keycodes[KEY_J] = KEY_CODE_J;
	wl->keycodes[KEY_K] = KEY_CODE_K;
	wl->keycodes[KEY_L] = KEY_CODE_L;
	wl->keycodes[KEY_SEMICOLON] = KEY_CODE_SEMICOLON;
	wl->keycodes[KEY_APOSTROPHE] = KEY_CODE_APOSTROPHE;
	wl->keycodes[KEY_Z] = KEY_CODE_Z;
	wl->keycodes[KEY_X] = KEY_CODE_X;
	wl->keycodes[KEY_C] = KEY_CODE_C;
	wl->keycodes[KEY_V] = KEY_CODE_V;
	wl->keycodes[KEY_B] = KEY_CODE_B;
	wl->keycodes[KEY_N] = KEY_CODE_N;
	wl->keycodes[KEY_M] = KEY_CODE_M;
	wl->keycodes[KEY_COMMA] = KEY_CODE_COMMA;
	wl->keycodes[KEY_DOT] = KEY_CODE_PERIOD;
	wl->keycodes[KEY_SLASH] = KEY_CODE_SLASH;
	wl->keycodes[KEY_BACKSLASH] = KEY_CODE_BACKSLASH;
	wl->keycodes[KEY_ESC] = KEY_CODE_ESCAPE;
	wl->keycodes[KEY_TAB] = KEY_CODE_TAB;
	wl->keycodes[KEY_LEFTSHIFT] = KEY_CODE_LEFTSHIFT;
	wl->keycodes[KEY_RIGHTSHIFT] = KEY_CODE_RIGHTSHIFT;
	wl->keycodes[KEY_LEFTCTRL] = KEY_CODE_LEFTCTRL;
	wl->keycodes[KEY_RIGHTCTRL] = KEY_CODE_RIGHTCTRL;
	wl->keycodes[KEY_LEFTALT] = KEY_CODE_LEFTALT;
	wl->keycodes[KEY_RIGHTALT] = KEY_CODE_RIGHTALT;
	wl->keycodes[KEY_LEFTMETA] = KEY_CODE_LEFTMETA;
	wl->keycodes[KEY_RIGHTMETA] = KEY_CODE_RIGHTMETA;
	wl->keycodes[KEY_COMPOSE] = KEY_CODE_MENU;
	wl->keycodes[KEY_NUMLOCK] = KEY_CODE_NUMLOCK;
	wl->keycodes[KEY_CAPSLOCK] = KEY_CODE_CAPSLOCK;
	wl->keycodes[KEY_PRINT] = KEY_CODE_PRINT;
	wl->keycodes[KEY_SCROLLLOCK] = KEY_CODE_SCROLLLOCK;
	wl->keycodes[KEY_PAUSE] = KEY_CODE_PAUSE;
	wl->keycodes[KEY_DELETE] = KEY_CODE_DELETE;
	wl->keycodes[KEY_BACKSPACE] = KEY_CODE_BACKSPACE;
	wl->keycodes[KEY_ENTER] = KEY_CODE_ENTER;
	wl->keycodes[KEY_HOME] = KEY_CODE_HOME;
	wl->keycodes[KEY_END] = KEY_CODE_END;
	wl->keycodes[KEY_PAGEUP] = KEY_CODE_PAGEUP;
	wl->keycodes[KEY_PAGEDOWN] = KEY_CODE_PAGEDOWN;
	wl->keycodes[KEY_INSERT] = KEY_CODE_INSERT;
	wl->keycodes[KEY_LEFT] = KEY_CODE_LEFT;
	wl->keycodes[KEY_RIGHT] = KEY_CODE_RIGHT;
	wl->keycodes[KEY_DOWN] = KEY_CODE_DOWN;
	wl->keycodes[KEY_UP] = KEY_CODE_UP;
	wl->keycodes[KEY_F1] = KEY_CODE_F1;
	wl->keycodes[KEY_F2] = KEY_CODE_F2;
	wl->keycodes[KEY_F3] = KEY_CODE_F3;
	wl->keycodes[KEY_F4] = KEY_CODE_F4;
	wl->keycodes[KEY_F5] = KEY_CODE_F5;
	wl->keycodes[KEY_F6] = KEY_CODE_F6;
	wl->keycodes[KEY_F7] = KEY_CODE_F7;
	wl->keycodes[KEY_F8] = KEY_CODE_F8;
	wl->keycodes[KEY_F9] = KEY_CODE_F9;
	wl->keycodes[KEY_F10] = KEY_CODE_F10;
	wl->keycodes[KEY_F11] = KEY_CODE_F11;
	wl->keycodes[KEY_F12] = KEY_CODE_F12;
	wl->keycodes[KEY_F13] = KEY_CODE_F13;
	wl->keycodes[KEY_F14] = KEY_CODE_F14;
	wl->keycodes[KEY_F15] = KEY_CODE_F15;
	wl->keycodes[KEY_F16] = KEY_CODE_F16;
	wl->keycodes[KEY_F17] = KEY_CODE_F17;
	wl->keycodes[KEY_F18] = KEY_CODE_F18;
	wl->keycodes[KEY_F19] = KEY_CODE_F19;
	wl->keycodes[KEY_F20] = KEY_CODE_F20;
	wl->keycodes[KEY_F21] = KEY_CODE_F21;
	wl->keycodes[KEY_F22] = KEY_CODE_F22;
	wl->keycodes[KEY_F23] = KEY_CODE_F23;
	wl->keycodes[KEY_F24] = KEY_CODE_F24;
	wl->keycodes[KEY_KPSLASH] = KEY_CODE_KPSLASH;
	wl->keycodes[KEY_KPASTERISK] = KEY_CODE_KPASTERISK;
	wl->keycodes[KEY_KPMINUS] = KEY_CODE_KPMINUS;
	wl->keycodes[KEY_KPPLUS] = KEY_CODE_KPPLUS;
	wl->keycodes[KEY_KP0] = KEY_CODE_KP0;
	wl->keycodes[KEY_KP1] = KEY_CODE_KP1;
	wl->keycodes[KEY_KP2] = KEY_CODE_KP2;
	wl->keycodes[KEY_KP3] = KEY_CODE_KP3;
	wl->keycodes[KEY_KP4] = KEY_CODE_KP4;
	wl->keycodes[KEY_KP5] = KEY_CODE_KP5;
	wl->keycodes[KEY_KP6] = KEY_CODE_KP6;
	wl->keycodes[KEY_KP7] = KEY_CODE_KP7;
	wl->keycodes[KEY_KP8] = KEY_CODE_KP8;
	wl->keycodes[KEY_KP9] = KEY_CODE_KP9;
	wl->keycodes[KEY_KPDOT] = KEY_CODE_KPDOT;
	wl->keycodes[KEY_KPEQUAL] = KEY_CODE_KPEQUAL;
	wl->keycodes[KEY_KPENTER] = KEY_CODE_KPENTER;
	wl->keycodes[KEY_102ND] = KEY_CODE_WORLD_1;
}
