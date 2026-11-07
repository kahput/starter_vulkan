#pragma once

#include "app/scene.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/geom.h"
#include "core/geom_types.h"
#include "core/input_types.h"
#include "core/mesh.h"
#include "core/strings.h"
#include "generated/resources.h"
#include "generated/res_generated.h"
#include "gpu/gpu_types.h"

#include "res.h"
#include "generated/resources.c"
#include "generated/res_generated.c"

#include <common.h>
#include <core/os.h>
#include <core/arena.h>
#include <core/logger.h>

#include <wm.h>

#include <gpu.h>

#include <draw.h>

#include <utils/input.h>
#include <utils/anim.h>

typedef struct Particle3 {
	float3 position, velocity;
	float t, lifetime;
} Particle3;

typedef struct {
	Transform3 transform;

	bool grounded;
	float3 acceleration, velocity;
} Player;

typedef struct {
	bool viewport_state_editor;
	Player player;

	Particle3 particles[128];
	float dust_accum;
	uint32_t particle_count;
} GameState;

typedef struct {
	Arena *permanent, *frame;

	WM_Surface *surface;
	GPU_Swapchain *swapchain;
	GPU_Image *depthbuffer;

	Input input;
	GPU_Image *white_texture;
	RES_Cache cache;

	GPU_Sampler *sampler_nearest;
	GPU_Sampler *sampler_linear;

	Camera camera;

	uint64_t start_time;
	float last_frame;

	GameState game;

	bool initialized;
} State;
State *state = 0;

static inline char *named(const char *name) {
	return (char *)str8f(state->permanent, "%s", name).bytes;
}

typedef struct {
	float2 position, grab_offset;
	bool active, hovered, initialized;
} Drag2D;

Rectangle drag2d_point(Drag2D *drag, float2 initial_position, float radius, float2 mouse) {
	Rectangle result = { 0 };

	bool ok = drag;
	if (ok) {
		if (drag->initialized == false) {
			drag->position = initial_position;
			drag->initialized = true;
		}

		Rectangle handle = rect_from_center(drag->position, float2(radius));
		drag->hovered = rect_contains_point(handle, mouse);

		if (drag->hovered && input_mouse_pressed(MOUSE_BUTTON_LEFT)) {
			drag->active = true;
            drag->grab_offset = sub2(drag->position, mouse);
        }

		if (drag->active) drag->position = add2(drag->grab_offset, mouse);
		if (input_mouse_released(MOUSE_BUTTON_LEFT)) drag->active = false;

		result = rect_from_center(drag->position, float2(radius));
	}

	return result;
}

Rectangle drag2d_slider(Drag2D *drag, Rectangle bounds, float min, float max, float *t) {
	Rectangle result = { 0 };

	bool ok = drag && t;
	if (ok) {
		*t = clamp(*t, min, max);
		float2 mouse = as2(input_mouse_position(), float2);

		float thumb_h = bounds.height;
		float thumb_w = thumb_h;

		float travel = bounds.width - thumb_w;

		drag->hovered = rect_contains_point(bounds, mouse);
		if (drag->hovered && input_mouse_pressed(MOUSE_BUTTON_LEFT)) drag->active = true;
		if (input_mouse_released(MOUSE_BUTTON_LEFT)) drag->active = false;

		if (drag->active) {
			float local_x = mouse.x - bounds.x - thumb_w * 0.5f;
			local_x = clamp(local_x, 0.0f, travel);

			float mouse_ratio = (travel != 0.0f) ? (local_x / travel) : 0.0f;
			*t = min + (mouse_ratio * (max - min));
		}

		float range = max - min;
		float t_norm = range != 0.0f ? (*t - min) / range : 0.0f;

		result = rect(bounds.x + t_norm * travel, bounds.y, thumb_w, thumb_h);
	}

	return result;
}

typedef struct {
	float3 position, grab_offset;
	bool active, hovered;
} Drag3D;

void drag3d_point(Drag3D *drag, float radius, Camera *camera, Rectangle viewport, float2 mouse) {
	Ray3 mouse_ray = camera_mouse_ray(camera, viewport, mouse);
	float3 forward = normalize3(sub3(camera->target, camera->position));

	float depth = dot3(sub3(drag->position, camera->position), forward);
	float a = depth / dot3(mouse_ray.direction, forward);
	float3 point = sadd3(mouse_ray.origin, mouse_ray.direction, a);

	drag->hovered = drag->active || raycast_sphere(mouse_ray, (Sphere){ drag->position, radius }).hit;
	if (drag->hovered && input_mouse_pressed(MOUSE_BUTTON_LEFT)) {
		drag->active = true;
		drag->grab_offset = sub3(drag->position, point);
	}
	if (drag->active) drag->position = add3(point, drag->grab_offset);
	if (input_mouse_released(MOUSE_BUTTON_LEFT)) drag->active = false;
}

const RES_Catalog registry = {
	.images = res_image_metadata,
	.image_count = RES_IMAGE_MAX,

	.fonts = res_font_metadata,
	.font_count = RES_FONT_MAX,

	.shaders = res_shader_metadata,
	.shader_count = RES_SHADER_MAX,
};
