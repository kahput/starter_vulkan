#include "app/scene.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/geom.h"
#include "core/geom_types.h"
#include "core/input_types.h"
#include "core/strings.h"
#include "draw/camera.h"
#include "gfx/gfx_types.h"
#include "res/tables.h"

#include <common.h>
#include <core/arena.h>
#include <core/logger.h>

#include <os.h>

#include <gfx.h>

#include <draw.h>
#include <draw/font.h>

#include <utils/input.h>
#include <utils/anim.h>

typedef struct {
	Arena *permanent, *frame;
	GFX_Device device[1];

	OS_Surface *surface;
	GFX_Swapchain *swapchain;
	GFX_Image *depthbuffer;

	InputState input;

	GFX_Shader *shaders[SHADER_MAX];
	OS_Timestamp shader_ts[SHADER_MAX];

	GFX_Image *white_texture;

	GFX_Sampler *nearest;

	Camera camera;

	uint64_t start_time;
	float last_frame;

	bool initialized;
} State;

State *state = 0;
static inline char *named(const char *name) {
	return (char *)str8_pushf(state->permanent, str8_wrap(name)).text;
}

typedef struct {
	float2 position;
	bool active, hovered, initialized;
} Drag2D;
Rectangle drag2d_point(Drag2D *drag, float2 initial_position, float radius) {
	Rectangle result = { 0 };

	bool ok = drag;
	if (ok) {
		float2 mouse = as2(input_mouse_position(), float2);
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

	bool ok = drag != 0 && t != 0;
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

bool tick(Arena *permanent, Arena *frame) {
	state = (State *)permanent->base;
	input_set_context(&state->input);

	if (state->initialized == false) {
		arena_push_count(permanent, State, 1); // reserve space for state
		state->permanent = permanent, state->frame = frame;

		gfx_device_make(state->device);

		state->surface = os_surface_open(1280, 720, str8_wrap(named("game")), OS_SURFACE_FLAG_RESIZEABLE);
		state->swapchain = gfx_swapchain_make(state->device, state->surface, named("main"));
		state->depthbuffer = gfx_image_make(state->device, 1280, 720,
			(ImageOptions){
			  .debug_name = named("target:main_depth"),
			  .format = PIXEL_FORMAT_DEPTH,
			  .usage = IMAGE_USAGE_RENDER,
			});

		for (uint32_t shaderid = 0; shaderid < SHADER_MAX; ++shaderid) {
			ShaderMetadata *metadata = &shaderid_to_metadata[shaderid];

			bool is_compute = metadata->filepaths[SHADER_STAGE_COMPUTE].length;
			if (is_compute) {
				String8 cs = os_file_read_entire(frame, metadata->filepaths[SHADER_STAGE_COMPUTE]);
				state->shaders[shaderid] = gfx_compute_make(state->device, cs, (char *)metadata->name.text);
				state->shader_ts[shaderid] = os_file_last_modified(metadata->filepaths[SHADER_STAGE_COMPUTE]);
			} else {
				String8 vs = os_file_read_entire(frame, metadata->filepaths[SHADER_STAGE_VERTEX]);
				String8 fs = os_file_read_entire(frame, metadata->filepaths[SHADER_STAGE_FRAGMENT]);

				OS_Timestamp fs_ts = os_file_last_modified(metadata->filepaths[SHADER_STAGE_FRAGMENT]);
				OS_Timestamp vs_ts = os_file_last_modified(metadata->filepaths[SHADER_STAGE_VERTEX]);

				state->shader_ts[shaderid] = MAX(fs_ts, vs_ts);

				state->shaders[shaderid] = gfx_shader_make(state->device, vs, fs, (char *)metadata->name.text);
				for (uint32_t permutation = 0; permutation < metadata->pipeline_count; ++permutation) {
					PipelineOptions opts = metadata->pipelines[permutation];
					opts.color_attachments[0] = PIXEL_FORMAT_BGRA8_UNORM;
					opts.sample_count = 1;
					gfx_pipeline_ensure(state->device, state->shaders[shaderid], opts);
				}
			}
		}

		state->white_texture = gfx_image_make(state->device, 1, 1, (ImageOptions){ .pixels = (uint8_t[]){ 255, 255, 255, 255 } });
		state->nearest = gfx_sampler_make(state->device, sampler_opt(named("sampler:nearest"), FILTER_NEAREST, WRAP_MODE_CLAMP));

		state->camera = (Camera){
			.projection = CAMERA_PROJECTION_PERSPECTIVE,
			.position = { 0.0f, 1.5f, 96.f },
			.target = { 0.0f, 1.5f, 0.0f },
			.up = unit3(UP),
			.fovy = 45.f,
			.near = 0.1f,
			.far = 500.0f,
		};

		state->last_frame = 0.0f;

		state->initialized = true;
		state->start_time = os_time_ns();
	}
	Camera *camera = &state->camera;

	double time = (os_time_ns() - state->start_time) * 1e-9;
	float dt = time - state->last_frame;
	state->last_frame = time;

	input_update();

	uint2 resize = { 0 };
	for (OS_Event event; os_event_poll(&event);) {
		switch (event.type) {
			case OS_EVENT_TYPE_SURFACE_CLOSE:
				return false;
				break;
			case OS_EVENT_TYPE_SURFACE_RESIZE:
				resize.x = event.as.resize.width;
				resize.y = event.as.resize.height;
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

	uint2 dims = os_surface_size(state->surface);
	Rectangle viewport = {
		.width = dims.x, .height = dims.y
	};

	float2 mouse_delta = as2(input_mouse_delta(), float2);
	float2 mouse_position = as2(input_mouse_position(), float2);
	mouse_delta.x /= dims.x;
	mouse_delta.y /= dims.y;

	float4x4 view = camera_view(&state->camera);
	float4x4 proj = camera_proj(&state->camera, viewport.width / viewport.height);
	float4x4 view_proj = mul4x4(proj, view);
	scene_camera_orbit(camera, mouse_delta);

	Arena line3d[] = { {
	  .base = arena_push_count(frame, DRAW_Line3D, 8096),
	  .capacity = sizeof(DRAW_Line3D) * 8096,
	} };
	Arena quad2d[] = { {
	  .base = arena_push_count(frame, DRAW_Quad2D, 8096),
	  .capacity = sizeof(DRAW_Quad2D) * 8096,
	} };
	Arena quad3d[] = { {
	  .base = arena_push_count(frame, DRAW_Quad3D, 1024),
	  .capacity = sizeof(DRAW_Quad3D) * 1024,
	} };

	/* int32_t seg_count = 16; */
	/* for (int32_t z = -seg_count; z <= seg_count; ++z) */
	/* 	draw3d_line(line3d, make3(-seg_count, 0.0f, z), make3(seg_count, 0.0f, z), 1.0f, z == 0 ? BLUE : GRAY); */
	/* for (int32_t x = -seg_count; x <= seg_count; ++x) */
	/* 	draw3d_line(line3d, make3(x, 0.0f, -seg_count), make3(x, 0.0f, seg_count), 1.0f, x == 0 ? RED : GRAY); */

	/* draw3d_sphere_outline(line3d, make3(0.0f, 3.0f, 0.0f), 3.0f, 32, 2.0f, BLACK); */

	float segment_size = 32.0f;
	float2 origo = rect_center(viewport);

	int2 counts = {
		(int32_t)ceilf(viewport.width * 0.5f / segment_size),
		(int32_t)ceilf(viewport.height * 0.5f / segment_size)
	};

	for (int32_t x = -counts.x; x <= counts.x; ++x) {
		float px = origo.x + x * segment_size;

		Color c = x == 0 ? BLUE : GRAY;
		draw2d_line(quad2d, make2(px, 0.0f), make2(px, viewport.height), 1.0f, c);
	}

	for (int32_t y = -counts.y; y <= counts.y; ++y) {
		float py = origo.y + y * segment_size;

		Color c = y == 0 ? RED : GRAY;
		draw2d_line(quad2d, make2(0.0f, py), make2(viewport.width, py), 1.0f, c);
	}

	static Drag2D drag_blue = { 0 }, drag_red = { 0 }, slider = { 0 };
	Rectangle blue = drag2d_point(&drag_blue, add2(origo, make2(-segment_size, 0.0f)), segment_size * 0.25f);
	draw2d_quad(quad2d, blue,
		(DRAW_QuadStyle){
		  .fill_color = drag_blue.active ? rgba(16, 40, 120, 160) : (drag_blue.hovered ? rgba(64, 110, 220, 110) : rgba(20, 40, 80, 90)),
		  .radii = splat4(segment_size * 0.25f),
		});

	Rectangle red = drag2d_point(&drag_red, add2(origo, make2(segment_size, 0.0f)), segment_size * 0.25f);
	draw2d_quad(quad2d, red,
		(DRAW_QuadStyle){
		  .fill_color = drag_red.active ? rgba(120, 16, 16, 160) : (drag_red.hovered ? rgba(220, 64, 64, 110) : rgba(80, 20, 20, 90)),
		  .radii = splat4(segment_size * 0.25f),
		});

	static float radius = 1.0f;
	Rectangle slider_rect = rect2(splat2(16.0f), make2(256.0f, 24.0f));
	draw2d_rect(quad2d, slider_rect, ORANGE);
	draw2d_quad(quad2d,
		drag2d_slider(&slider, rect_padded(slider_rect, splat4(4.0f)), 0.0f, 8.0f, &radius),
		(DRAW_QuadStyle){
		  .fill_color = RED,
		});

	bool is_inside = distsq2(origo, rect_center(blue)) <= sqf(radius * segment_size);
	draw2d_circle_outline(quad2d, origo, radius * segment_size, 3.0f, is_inside ? GREEN : RED);

	float2 blue_to_red = norm2(sub2(rect_center(red), rect_center(blue)));
	draw2d_arrow(quad2d, rect_center(blue), scale2(blue_to_red, segment_size), 2.0f, segment_size * 0.25f, BLACK);

	GFX_Device *device = state->device;
	GFX_Swapchain *swapchain = state->swapchain;

	if (resize.x && resize.y) {
		gfx_swapchain_resize(device, swapchain, resize.x, resize.y);
		gfx_image_resize(device, state->depthbuffer, resize.x, resize.y);
	}

	GFX_CommandEncoder *cmd = gfx_frame_begin(state->device);
	if (cmd == 0) return false;

	GFX_Image *backbuffer = gfx_swapchain_backbuffer(device, cmd, swapchain);
	if (backbuffer) {
		gfx_cmd_draw_begin(cmd,
			(GFX_DrawPassInfo){
			  .debug_name = "main",
			  .colors[0] = { .target = backbuffer, .load = LOAD_OP_CLEAR, .store = STORE_OP_STORE, .clear = WHITE },
			  .depth = { .target = state->depthbuffer, .clear = 1.0f, .load = LOAD_OP_CLEAR, .store = STORE_OP_STORE },
			});

		typedef struct {
			float4x4 view;
			float4x4 proj;
			float4 camera_position;
			float2 viewport;
			float fog_density;
			float ambient_strength;
			float fog_gradient;
			float time;
		} FrameData;

		FrameData fd = {
			.viewport = { dims.x, dims.y },
			.time = time,
			.view = view,
			.proj = proj,
			.camera_position = make4_from3(state->camera.position, 0.0f),
		};
		Uniform set0[] = { uniform_data(0, &fd, sizeof(fd)) };
		gfx_cmd_bind(device, 0, set0, countof(set0));

		if (line3d->offset) {
			gfx_cmd_shader_bind(cmd, state->shaders[SHADER_LINE3D]);

			Uniform set1[] = {
				storage_data(0, line3d->base, line3d->offset),
			};
			gfx_cmd_bind(device, 0, set0, countof(set0));
			gfx_cmd_bind(device, 1, set1, countof(set1));
			gfx_cmd_draw_instanced(cmd, 0, 6, 0, line3d->offset / sizeof(DRAW_Line3D));
		}
		if (quad3d->offset) {
			uint32_t quad_count = quad3d->offset / sizeof(DRAW_Quad3D);
			GFX_Image *images[32] = { 0 };
			uint32_t image_count = 1;
			for (uint32_t texture_id = 0; texture_id < 32; ++texture_id)
				images[texture_id] = state->white_texture;

			for (uint32_t quad_instance = 0; quad_instance < quad_count; ++quad_instance) {
				DRAW_Quad3D *quad = (DRAW_Quad3D *)quad3d->base + quad_instance;

				if (quad->imageid && quad->imageid != indexof(device->image_pool, state->white_texture)) {
					int32_t found_index = -1;
					for (uint32_t image_index = 1; image_index < image_count; ++image_index) {
						if (indexof(device->image_pool, images[image_index]) == quad->imageid) {
							found_index = image_index;
							break;
						}
					}

					if (found_index == -1) {
						ASSERT(image_count < countof(images) && "Extend sprite batching to support beyond 32 distinct images");
						found_index = image_count++;
						images[found_index] = &device->image_pool[quad->imageid];
						gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, device->image_pool + quad->imageid);
					}

					quad->imageid = found_index;
				}
			}

			gfx_cmd_shader_bind(cmd, state->shaders[SHADER_QUAD3D]);

			Uniform set1[] = {
				storage_data(1, quad3d->base, quad3d->offset),
				sampler_with_textures(0, images, countof(images), state->nearest),
			};

			gfx_cmd_bind(device, 0, set0, countof(set0));
			gfx_cmd_bind(device, 1, set1, countof(set1));

			gfx_cmd_draw_instanced(cmd, 0, 6, 0, quad_count);
		}

		if (quad2d->offset) {
			FrameData fd = {
				.view = identity4x4(),
				.proj = orthographic(0.0f, dims.x, 0.0f, dims.y, -50.f, 50.f),
				.viewport = as2(dims, float2),
				.time = time,
			};
			uint32_t quad_count = quad2d->offset / sizeof(DRAW_Quad2D);

			GFX_Image *images[32] = { 0 };
			uint32_t image_count = 1;
			for (uint32_t texture_id = 0; texture_id < 32; ++texture_id)
				images[texture_id] = state->white_texture;

			for (uint32_t quad_instance = 0; quad_instance < quad_count; ++quad_instance) {
				DRAW_Quad2D *quad = (DRAW_Quad2D *)quad2d->base + quad_instance;

				if (quad->imageid && quad->imageid != indexof(device->image_pool, state->white_texture)) {
					int32_t found_index = -1;
					for (uint32_t image_index = 1; image_index < image_count; ++image_index) {
						if (indexof(device->image_pool, images[image_index]) == quad->imageid) {
							found_index = image_index;
							break;
						}
					}

					if (found_index == -1) {
						ASSERT(image_count < countof(images) && "Extend sprite batching to support beyond 32 distinct images");
						found_index = image_count++;
						images[found_index] = &device->image_pool[quad->imageid];
						gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, device->image_pool + quad->imageid);
					}

					quad->imageid = found_index;
				}
			}

			gfx_cmd_shader_bind(cmd, state->shaders[SHADER_QUAD2D]);

			Uniform uniforms0[] = {
				uniform_data(0, &fd, sizeof(fd)),
				storage_data(1, quad2d->base, quad2d->offset),
			};
			Uniform uniforms1[] = { sampler_with_textures(0, images, countof(images), state->nearest) };

			gfx_cmd_bind(device, 0, uniforms0, countof(uniforms0));
			gfx_cmd_bind(device, 1, uniforms1, countof(uniforms1));

			gfx_cmd_draw_instanced(cmd, 0, 6, 0, quad_count);
		}

		gfx_cmd_draw_end(cmd);
	}

	gfx_frame_end(device, cmd);

	for (ShaderID shaderid = 0; shaderid < SHADER_MAX; ++shaderid) { // :hot-reload
		ShaderMetadata *metadata = &shaderid_to_metadata[shaderid];
		if (metadata->filepaths[SHADER_STAGE_VERTEX].length == 0 &&
			metadata->filepaths[SHADER_STAGE_FRAGMENT].length == 0 &&
			metadata->filepaths[SHADER_STAGE_COMPUTE].length == 0)
			continue;

		bool is_compute = metadata->filepaths[SHADER_STAGE_COMPUTE].length > 0;
		if (is_compute) {
			OS_Timestamp now = os_file_last_modified(metadata->filepaths[SHADER_STAGE_COMPUTE]);

			if (now != state->shader_ts[shaderid]) {
				LOG_INFO("hot-reloading %s...", state->shaders[shaderid]->debug_name);
				gfx_device_wait_idle(device);

				gfx_shader_destroy(device, state->shaders[shaderid]);
				String8 bytecode = os_file_read_entire(frame, metadata->filepaths[SHADER_STAGE_COMPUTE]);
				state->shaders[shaderid] = gfx_compute_make(device, bytecode, (char *)shaderid_to_string[shaderid].text);

				state->shader_ts[shaderid] = now;
			}
		} else {
			OS_Timestamp fs_ts = os_file_last_modified(metadata->filepaths[SHADER_STAGE_FRAGMENT]);
			OS_Timestamp vs_ts = os_file_last_modified(metadata->filepaths[SHADER_STAGE_VERTEX]);

			OS_Timestamp now = MAX(fs_ts, vs_ts);
			if (now != state->shader_ts[shaderid]) {
				LOG_INFO("hot-reloading %s...", state->shaders[shaderid]->debug_name);
				gfx_device_wait_idle(device);

				ShaderMetadata *metadata = &shaderid_to_metadata[shaderid];

				gfx_shader_destroy(device, state->shaders[shaderid]);

				String8 vs_bytecode = os_file_read_entire(frame, metadata->filepaths[SHADER_STAGE_VERTEX]);
				String8 fs_bytecode = os_file_read_entire(frame, metadata->filepaths[SHADER_STAGE_FRAGMENT]);
				state->shaders[shaderid] = gfx_shader_make(device, vs_bytecode, fs_bytecode, (char *)shaderid_to_string[shaderid].text);

				for (uint32_t permutation = 0; permutation < metadata->pipeline_count; ++permutation) {
					PipelineOptions opts = metadata->pipelines[permutation];
					opts.color_attachments[0] = PIXEL_FORMAT_BGRA8_UNORM;
					opts.sample_count = 1;
					gfx_pipeline_ensure(device, state->shaders[shaderid], opts);
				}

				state->shader_ts[shaderid] = now;
			}
		}
	}

	return true;
}
