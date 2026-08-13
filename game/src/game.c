#include "core/debug.h"
#include "core/input_types.h"
#include "core/strings.h"
#include "gfx/gfx_types.h"
#include "tables.h"
#include "types.h"

#include <common.h>
#include <core/arena.h>
#include <core/logger.h>

#include <os.h>

#include <gfx.h>

#include <draw.h>
#include <draw/font.h>
#include <draw/imgui.h>

#include <unistd.h>
#include <utils/input.h>
#include <utils/anim.h>

#include "helper.c"

typedef struct {
	Arena *permanent, *frame;
	GFX_Device device[1];

	OS_Surface *surface;
	GFX_Swapchain *swapchain;

	InputState input;

	GFX_Shader *shaders[SHADER_MAX];
	OS_Timestamp shader_ts[SHADER_MAX];

	GFX_Image *default_texture;
	Font default_font;
	GFX_Sampler *nearest;

	GFX_Image *target;

	IMGUI_Context imgui;

	uint64_t start_time;
	float dt, last_frame;

	bool initialized;
} State;

State *state = 0;
static inline char *named(const char *name) {
	return (char *)str8_pushf(state->permanent, str8_wrap(name)).text;
}

bool tick(Arena *permanent, Arena *frame) {
	state = (State *)permanent->base;
	input_set_context(&state->input);

	if (state->initialized == false) {
		arena_push_count(permanent, State, 1); // reserve space for state
		state->permanent = permanent, state->frame = frame;

		gfx_device_make(state->device);

		state->surface = os_surface_open(1280, 720, str8_wrap(named("game")), OS_SURFACE_FLAG_RESIZEABLE);
		state->swapchain = gfx_swapchain_make(state->device, state->surface, named("main"));

		for (ShaderID id = 0; id < SHADER_MAX; ++id)
			load_or_reload_shader(state->device, id, state->shaders, state->shader_ts);

		state->default_texture = gfx_image_make(state->device, 1, 1, (ImageOptions){ .pixels = (uint32_t[]){ 0xFFFFFFFF } });
		state->nearest = gfx_sampler_make(state->device, sampler_opt(named("nearest:clamp_border"), FILTER_NEAREST, WRAP_MODE_CLAMP_BORDER));
		state->target = gfx_image_make(state->device, 1280, 720,
			(ImageOptions){
			  .debug_name = named("target:color"),
			  .format = shader_to_metadata[SHADER_QUAD2D].pipelines[0].color_attachments[0],
			  .usage = IMAGE_USAGE_RENDER | IMAGE_USAGE_TRANSFER,
			});

		state->default_font = load_font(state->permanent,
			s("/usr/share/fonts/TTF/IBMPlexMono-Regular.ttf"), 16);
		state->default_font.atlas.handle = gfx_image_make(state->device, state->default_font.atlas.width, state->default_font.atlas.height,
			(ImageOptions){
			  .debug_name = "default_font:16",
			  .format = PIXEL_FORMAT_RGBA8_UNORM,
			  .usage = IMAGE_USAGE_SAMPLE | IMAGE_USAGE_TRANSFER,
			  .pixels = state->default_font.atlas.pixels });
		state->imgui.default_font = &state->default_font;

		state->initialized = true;
		state->start_time = os_time_ns();
	}
	float time = (os_time_ns() * 1e-9) - (state->start_time * 1e-9);
	state->dt = time - state->last_frame;
	state->last_frame = time;
	input_update();

	uint2 resize = { 0 };
	for (OS_Event event; os_event_poll(&event);) {
		switch (event.type) {
			case OS_EVENT_TYPE_SURFACE_CLOSE:
				return false;
				break;
			case OS_EVENT_TYPE_SURFACE_RESIZE:
				resize.x = event.as.resize.width, resize.y = event.as.resize.height;
				break;

			case OS_EVENT_TYPE_KEY_PRESS:
			case OS_EVENT_TYPE_KEY_RELEASE:
				input_feed_key(event.as.key.key_code, event.type == OS_EVENT_TYPE_KEY_PRESS);
				break;

			case OS_EVENT_TYPE_MOUSE_MOVE:
				input_feed_mouse_motion(event.as.mouse_move.x, event.as.mouse_move.y);
				break;

			case OS_EVENT_TYPE_MOUSE_PRESS:
			case OS_EVENT_TYPE_MOUSE_RELEASE:
				if (event.type == OS_EVENT_TYPE_MOUSE_RELEASE) {
					uint32_t x = 0;
					(void)x;
				}
				input_feed_mouse_button(event.as.mouse_button.button, event.type == OS_EVENT_TYPE_MOUSE_PRESS);
				break;

			default:
				break;
		}
	}

	if (resize.x && resize.y) {
		gfx_swapchain_resize(state->device, state->swapchain, resize.x, resize.y);
		gfx_image_resize(state->device, state->target, resize.x, resize.y);
	}

	uint2 dims = os_surface_size(state->surface);
	float2 mouse = cast2(input_mouse_position(), float2);
	Rectangle viewport = { 0.0f, 0.0f, dims.x, dims.y };

	imgui_frame_begin(&state->imgui,
		(IMGUI_Mouse){
		  .last_position = state->imgui.mouse.position,
		  .position = mouse,
		  .pressed[MOUSE_BUTTON_LEFT] = input_mouse_pressed(MOUSE_BUTTON_LEFT),
		  .released[MOUSE_BUTTON_LEFT] = input_mouse_released(MOUSE_BUTTON_LEFT),
		},
		state->dt);

	IMGUI_Widget *root = imgui_widget_opt(0,
		(IMGUI_Style){
		  .align = { IMGUI_ALIGN_CENTER, IMGUI_ALIGN_CENTER },
		  .sizing = { IMGUI_SIZING_FIXED, IMGUI_SIZING_FIXED },
		});
	store2(cast2(dims, float2), root->size);
	imgui_push_parent(root);

	IMGUI_Widget *con = imgui_widget_opt(0,
		(IMGUI_Style){
		  .flow = IMGUI_VERTICAL,
		  .bg = hex(0x262c36),
          .gap = 4.0f,
		});
	imgui_push_parent(con);
	imgui_push_style((IMGUI_Style){ .fg = WHITE });

	imgui_label(0, s("Day"));
	imgui_label(0, s("Dawn"));
	imgui_label(0, s("Night"));

	imgui_pop_style(1);
	imgui_pop_parent();
	imgui_pop_parent();

	imgui_layout(root);

	/* IMGUI_Widget *root = imgui_widget_opt(0, */
	/* (IMGUI_Style){ */
	/* .sizing = { IMGUI_SIZING_FIXED, IMGUI_SIZING_FIXED }, */
	/* .align[0] = IMGUI_ALIGN_RIGHT, */
	/* .p = 12.0f, */
	/* }); */
	/* store2(cast2(dims, float2), root->size); */
	/* imgui_push_parent(root); */

	/* float p = 18.0f; */

	/* IMGUI_Widget *con = imgui_widget_opt(0, */
	/* (IMGUI_Style){ */
	/* .flow = IMGUI_HORIZONTAL, */
	/* .sizing = { IMGUI_SIZING_FIT, IMGUI_SIZING_FIT }, */
	/* .align[0] = IMGUI_ALIGN_RIGHT, */
	/* .bg = RED, */
	/* .p = p, */
	/* .gap = p }); */
	/* imgui_push_parent(con); */
	/* for (uint32_t col = 0; col < 8; ++col) { */
	/* imgui_push_parent( */
	/* imgui_widget_opt(0, */
	/* (IMGUI_Style){ */
	/* .flow = IMGUI_VERTICAL, */
	/* .sizing = { IMGUI_SIZING_FIT, IMGUI_SIZING_FIT }, */
	/* .align[0] = IMGUI_ALIGN_RIGHT, */
	/* .gap = p, */
	/* })); */

	/* for (uint32_t row = 0; row < 4; ++row) { */
	/* IMGUI_Widget *slot = imgui_widget_opt(hash64_combine(__LINE__, row + col * 4), */
	/* (IMGUI_Style){ */
	/* .sizing = { IMGUI_SIZING_FIXED, IMGUI_SIZING_FIXED }, */
	/* .bg = rgba(48, 128 + 48, 128 + 48, 192), */
	/* }); */
	/* store2(splat2(64.0f), slot->size); */

	/* IMGUI_Interact i = imgui_interact(slot->id, imgui_rect_cached(slot)); */

	/* static ANIM_Tween1f scale_tweens[8 * 4] = { 0 }; */
	/* if (i.held) { */
	/* slot->settings.bg = ORANGE; */
	/* slot->visual.scale = tween1f_update(scale_tweens + (row + col * 4), 0.95f, 0.05f, state->dt); */
	/* } else if (i.hovered) { */
	/* slot->settings.bg = ORANGE; */
	/* slot->visual.scale = tween1f_update(scale_tweens + (row + col * 4), 1.15f, 0.2f, state->dt); */
	/* } else */
	/* slot->visual.scale = tween1f_update(scale_tweens + (row + col * 4), 1.0f, 0.5f, state->dt); */
	/* } */
	/* imgui_pop_parent(); */
	/* } */
	/* imgui_pop_parent(); */

	/* imgui_pop_parent(); */
	/* imgui_layout(root); */

	for (uint32_t index = 0; index < state->imgui.widget_count; ++index) {
		IMGUI_Widget *widget = &state->imgui.widgets[index];
		if (imgui_valid(widget)) {
			IMGUI_Widget *parent = &state->imgui.widgets[widget->parent];
			// TODO: Scissor
			/* if (imgui_valid(parent)) */
			/* 	BeginScissorMode(parent->offset[0], parent->offset[1], parent->size[0], parent->size[1]); */

			Rectangle rect = imgui_rect_live(widget);

			float scale[] = {
				widget->size[0] * (widget->visual.scale ? widget->visual.scale : 1.0f),
				widget->size[1] * (widget->visual.scale ? widget->visual.scale : 1.0f)
			};

			rect.x -= (scale[0] - widget->size[0]) * 0.5f;
			rect.y -= (scale[1] - widget->size[1]) * 0.5f;

			rect.width = scale[0];
			rect.height = scale[1];

			if (widget->settings.image) {
				draw2d_quad(
					frame,
					rect,
					image_rect(*widget->settings.image),
					widget->settings.image,
					(float2){ 0 },
					0.0f,
					widget->settings.border_width, widget->settings.border, widget->settings.border_radius, widget->settings.fg);
			} else if (widget->settings.text.length) {
				Font *font = widget->settings.font ? widget->settings.font : state->imgui.default_font;
				draw2d_quad(
					frame,
					rect,
					(Rectangle){ 0 },
					0,
					splat2(0.0f),
					0.0f,
					0, TRANSPARENT, splat4(0.0f), ORANGE);
				draw2d_textf(frame, font, make2(rect.x, rect.y), widget->settings.fg, widget->settings.text);
			} else {
				draw2d_quad(
					frame,
					rect,
					(Rectangle){ 0 },
					0, (float2){ 0 }, 0.0f,
					widget->settings.border_width, widget->settings.border,
					widget->settings.border_radius, widget->settings.bg);
			}
			/* if (imgui_valid(parent)) */
			/* 	EndScissorMode(); */
		}
	}
	imgui_frame_end();

	GFX_Command *cmd = gfx_frame_begin(state->device);
	if (cmd == 0) return false;

	GFX_Image *backbuffer = gfx_backbuffer(state->device, cmd, state->swapchain);
	if (state->target) {
		gfx_cmd_draw_begin(cmd,
			(GFX_DrawPassInfo){
			  .debug_name = "Pass2D",
			  .colors[0] = {
				.target = state->target,
				.load = LOAD_OP_CLEAR,
				.clear = WHITE,
			  },
			});

		if (frame->offset) {
			gfx_cmd_shader_bind(cmd, state->shaders[SHADER_QUAD2D]);
			typedef struct {
				float4x4 view;
				float4x4 projection;
				float2 camera_position;
				float2 viewport;
				float time;
			} Frame2D;

			Frame2D frame_2d = {
				.view = identity4x4(),
				.projection = orthographic(0.0f, dims.x, 0.0f, dims.y, -50.f, 50.f),
				.viewport = cast2(dims, float2),
				.time = time,
			};

			Uniform uniforms0[] = {
				uniform_data(0, &frame_2d, sizeof(frame_2d)),
				storage_data(1, frame->base, frame->offset),
			};

			GFX_Image *images[32] = { 0 };
			uint32_t image_count = 1;
			for (uint32_t texture_id = 0; texture_id < 32; ++texture_id)
				images[texture_id] = state->default_texture;

			uint32_t vertex_count = frame->offset / sizeof(QuadVertex2D);
			uint32_t quad_count = vertex_count / 6;

			for (uint32_t quad_index = 0; quad_index < quad_count; ++quad_index) {
				QuadVertex2D *quad_first_vertex = (QuadVertex2D *)frame->base + (quad_index * 6);

				if (quad_first_vertex->imageid && quad_first_vertex->imageid != indexof(state->device->image_pool, state->default_texture)) {
					int32_t found_index = -1;
					for (uint32_t image_index = 1; image_index < image_count; ++image_index) {
						if (indexof(state->device->image_pool, images[image_index]) == quad_first_vertex->imageid) {
							found_index = image_index;
							break;
						}
					}

					if (found_index == -1) {
						ASSERT(image_count < countof(images) && "Extend sprite batching to support beyond 32 distinct images");
						found_index = image_count++;
						images[found_index] = &state->device->image_pool[quad_first_vertex->imageid];
						gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, state->device->image_pool + quad_first_vertex->imageid);
					}

					for (uint32_t vertex_index = 0; vertex_index < 6; ++vertex_index) {
						QuadVertex2D *vertex = quad_first_vertex + vertex_index;
						vertex->imageid = found_index;
					}
				}
			}
			Uniform uniforms1[] = { sampler_with_textures(0, images, countof(images), state->nearest) };

			gfx_cmd_bind(state->device, 0, uniforms0, countof(uniforms0));
			gfx_cmd_bind(state->device, 1, uniforms1, countof(uniforms1));

			gfx_cmd_draw(cmd, vertex_count, 0);
		}
		gfx_cmd_draw_end(cmd);

		gfx_cmd_image_blit(cmd, (Rectangle){ 0 }, state->target, (Rectangle){ 0 }, backbuffer);
	}

	gfx_frame_end(state->device, cmd);

	return true;
}
