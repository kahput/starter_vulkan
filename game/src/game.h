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
#include "gfx/gfx_types.h"

#include "res.h"
#include "generated/resources.c"
#include "generated/res_generated.c"

#include <common.h>
#include <core/arena.h>
#include <core/logger.h>

#include <os.h>

#include <gfx.h>

#include <draw.h>

#include <utils/input.h>
#include <utils/anim.h>
typedef struct {
	float3 position, velocity;
	float t;
} Particle3;

typedef struct {
	bool viewport_state_editor;
	float3 player_position;

	Particle3 particles[256];
	uint32_t particle_next;
} GameState;

typedef struct {
	Arena *permanent, *frame;

	GFX_Device device[1];

	OS_Surface *surface;
	GFX_Swapchain *swapchain;
	GFX_Image *depthbuffer;

	InputState input;
	GFX_Image *white_texture;
	RES_Cache cache;

	GFX_Sampler *sampler_nearest;
	GFX_Sampler *sampler_linear;

	Camera camera;

	uint64_t start_time;
	float last_frame;

	GameState game;

	bool initialized;
} State;
State *state = 0;

static inline char *named(const char *name) {
	return (char *)fmt8(state->permanent, name).bytes;
}

typedef struct {
	float2 position;
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

		Rectangle handle = rect_from_center(drag->position, splat2(radius));
		drag->hovered = rect_contains_point(handle, mouse);

		if (drag->hovered && input_mouse_pressed(MOUSE_BUTTON_LEFT))
			drag->active = true;

		if (drag->active) drag->position = mouse;
		if (input_mouse_released(MOUSE_BUTTON_LEFT)) drag->active = false;

		result = rect_from_center(drag->position, splat2(radius));
	}

	return result;
}

Rectangle drag2d_slider(Drag2D *drag, Rectangle bounds, float min, float max, float *t) {
	Rectangle result = { 0 };

	bool ok = drag && t;
	if (ok) {
		*t = clampf(*t, min, max);
		float2 mouse = as2(input_mouse_position(), float2);

		float thumb_h = bounds.height;
		float thumb_w = thumb_h;

		float travel = bounds.width - thumb_w;

		drag->hovered = rect_contains_point(bounds, mouse);
		if (drag->hovered && input_mouse_pressed(MOUSE_BUTTON_LEFT)) drag->active = true;
		if (input_mouse_released(MOUSE_BUTTON_LEFT)) drag->active = false;

		if (drag->active) {
			float local_x = mouse.x - bounds.x - thumb_w * 0.5f;
			local_x = clampf(local_x, 0.0f, travel);

			float mouse_ratio = (travel != 0.0f) ? (local_x / travel) : 0.0f;
			*t = min + (mouse_ratio * (max - min));
		}

		float range = max - min;
		float t_norm = range != 0.0f ? (*t - min) / range : 0.0f;

		result = rect(bounds.x + t_norm * travel, bounds.y, thumb_w, thumb_h);
	}

	return result;
}

const RES_Catalog registry = {
	.images = res_image_metadata,
	.image_count = RES_IMAGE_MAX,

	.fonts = res_font_metadata,
	.font_count = RES_FONT_MAX,

	.shaders = res_shader_metadata,
	.shader_count = RES_SHADER_MAX,
};
