#include "gfx/gfx_types.h"
#include "types.h"

#include <common.h>
#include <core/arena.h>
#include <core/logger.h>

#include <os.h>

#include <gfx.h>

#include <draw.h>
#include <draw/font.h>
#include <draw/imgui.h>

#include <utils/input.h>
#include <utils/anim.h>

#include "helper.c"

typedef struct {
	GFX_Device device[1];

	OS_Surface *surface, *popup;
	GFX_Swapchain *swapchain, *swapchain_popup;

	GFX_Shader *shaders[SHADER_MAX];
	OS_Timestamp shader_ts[SHADER_MAX];

	GFX_Image *compute;

	Mesh meshes[MESH_MAX];

	bool initialized;
} State;

State *state = 0;

bool tick(Arena *permanent, Arena *frame) {
	state = (State *)permanent->base;

	if (state->initialized == false) {
		gfx_device_make(state->device);

		state->surface = os_surface_open(1280, 720, s("game"), OS_SURFACE_FLAG_RESIZEABLE);
		state->popup = os_surface_open(640, 180, s("popup"), 0);

		state->swapchain = gfx_swapchain_make(state->device, state->surface, "main");
		state->swapchain_popup = gfx_swapchain_make(state->device, state->popup, "popup");

		for (ShaderID id = 0; id < SHADER_MAX; ++id)
			load_or_reload_shader(state->device, id, state->shaders, state->shader_ts);

		state->compute = gfx_image_make(state->device, 640, 180,
			(ImageOptions){
			  .debug_name = "target:compute",
			  .format = PIXEL_FORMAT_RGBA16_FLOAT,
			  .usage = IMAGE_USAGE_STORAGE | IMAGE_USAGE_TRANSFER,
			});

		state->initialized = true;
	}

	float2 popup_mouse = { 0 };
	for (OS_Event event; os_event_poll(&event);) {
		switch (event.type) {
			case OS_EVENT_TYPE_MOUSE_MOVE:
				if (event.surface == state->popup)
					popup_mouse.x = event.as.mouse_move.x, popup_mouse.y = event.as.mouse_move.y;
				break;
			case OS_EVENT_TYPE_SURFACE_CLOSE:
				return false;
			case OS_EVENT_TYPE_SURFACE_RESIZE:
				if (event.surface == state->surface)
					gfx_swapchain_resize(state->device, state->swapchain, event.as.resize.width, event.as.resize.height);
				else {
					gfx_swapchain_resize(state->device, state->swapchain_popup, event.as.resize.width, event.as.resize.height);
					gfx_image_resize(state->device, state->compute, event.as.resize.width, event.as.resize.height);
				}
				break;
			default:
				break;
		}
	}

	GFX_Command *cmd = gfx_frame_begin(state->device);
	if (cmd == 0) return false;

	float time = os_time_ns() * 1e-9;

	GFX_Image *popup = gfx_backbuffer(state->device, cmd, state->swapchain_popup);
	if (popup) {
		gfx_cmd_image_transition(cmd, RESOURCE_USAGE_COMPUTE_SHADER_WRITE, state->compute);
		gfx_cmd_shader_bind(cmd, state->shaders[SHADER_TEST_COMPUTE]);
		gfx_cmd_bind(state->device, 0, array_arg(Uniform, storage_images(0, (GFX_Image *[]){ state->compute }, 1)));

		struct {
			float2 mouse;
			float time;
		} pc = {
			.mouse = popup_mouse,
			.time = (float)time,
		};

		gfx_cmd_push_constant(cmd, sizeof(pc), &pc);
		gfx_cmd_dispatch(cmd, (popup->width / 16) + 1, (popup->height / 16) + 1, 1);
		gfx_cmd_image_transition(cmd, RESOURCE_USAGE_TRANSFER_SRC, state->compute);
		gfx_cmd_image_blit(cmd, (Rectangle){ 0 }, state->compute, (Rectangle){ 0 }, popup);
	}

	GFX_Image *main = gfx_backbuffer(state->device, cmd, state->swapchain);
	if (main) {
		gfx_cmd_image_clear(cmd, (Rectangle){ 0 }, RED, main);
	}

	gfx_frame_end(state->device, cmd);

	return true;
}
