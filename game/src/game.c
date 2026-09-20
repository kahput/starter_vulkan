#include "app/scene.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/geom.h"
#include "core/geom_types.h"
#include "core/input_types.h"
#include "core/strings.h"
#include "draw/camera.h"
#include "gfx/gfx_types.h"

#include "res.h"
#include "generated/assets_generated.c"

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

	GFX_Shader *shaders[RES_SHADER_MAX];
	OS_Timestamp shader_ts[RES_SHADER_MAX];

	GFX_Image *white_texture;
	RES_Cache cache[1];

	GFX_Sampler *nearest;

	Camera camera;

	uint64_t start_time;
	float last_frame;

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

		state->surface = os_surface_open(1280, 720, str8z(named("game")), OS_SURFACE_FLAG_RESIZEABLE);
		state->swapchain = gfx_swapchain_make(state->device, state->surface, named("main"));
		state->depthbuffer = gfx_image_make(state->device, 1280, 720,
			(ImageOptions){
			  .debug_name = named("target:main_depth"),
			  .format = PIXEL_FORMAT_DEPTH,
			  .usage = IMAGE_USAGE_RENDER,
			});

		for (RES_ShaderID shaderid = 0; shaderid < RES_SHADER_MAX; ++shaderid) {
			ShaderMetadata *metadata = &res_shaderid_to_metadata[shaderid];

			bool is_compute = metadata->filepaths[SHADER_STAGE_COMPUTE].length;
			if (is_compute) {
				string8 cs = os_file_read(frame, metadata->filepaths[SHADER_STAGE_COMPUTE]);
				state->shaders[shaderid] = gfx_compute_make(state->device, cs, (char *)metadata->name.bytes);
				state->shader_ts[shaderid] = os_file_mtime(metadata->filepaths[SHADER_STAGE_COMPUTE]);
			} else {
				string8 vs = os_file_read(frame, metadata->filepaths[SHADER_STAGE_VERTEX]);
				string8 fs = os_file_read(frame, metadata->filepaths[SHADER_STAGE_FRAGMENT]);

				OS_Timestamp fs_ts = os_file_mtime(metadata->filepaths[SHADER_STAGE_FRAGMENT]);
				OS_Timestamp vs_ts = os_file_mtime(metadata->filepaths[SHADER_STAGE_VERTEX]);

				state->shader_ts[shaderid] = MAX(fs_ts, vs_ts);

				state->shaders[shaderid] = gfx_shader_make(state->device, vs, fs, (char *)metadata->name.bytes);
				for (uint32_t permutation = 0; permutation < metadata->pipeline_count; ++permutation) {
					PipelineOptions opts = metadata->pipelines[permutation];
					gfx_pipeline_ensure(state->device, state->shaders[shaderid], opts,
						(GFX_DrawTargetLayout){
						  .color_formats[0] = PIXEL_FORMAT_BGRA8_UNORM,
						  .color_count = 1,
						});
				}
			}
		}

		state->white_texture = gfx_image_make(state->device, 1, 1, (ImageOptions){ .pixels = (uint8_t[]){ 255, 255, 255, 255 } });
		state->nearest = gfx_sampler_make(state->device, sampler_opt(named("sampler:nearest"), FILTER_NEAREST, WRAP_MODE_CLAMP));

		Arena *resource_arena = arena_push_count(permanent, Arena, 1);
		resource_arena->base = arena_push(state->permanent, MiB(8), 16, true);
		resource_arena->capacity = MiB(8);
		state->cache[0] = res_init(resource_arena, state->device);

		state->camera = (Camera){
			.projection = CAMERA_PROJECTION_PERSPECTIVE,
			.position = { 0.0f, 3.0f, 8.f },
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
	Rectangle viewport = { .width = dims.x, .height = dims.y };

	float2 mouse_delta = as2(input_mouse_delta(), float2);
	float2 mouse_position = as2(input_mouse_position(), float2);
	mouse_delta.x /= dims.x;
	mouse_delta.y /= dims.y;

	DRAW_List *draw = drawlist_make(frame);
	scene_camera_orbit(camera, mouse_delta);

	float4x4 view_from_world = camera_view(&state->camera);
	float4x4 clip_from_view = camera_proj(&state->camera, viewport.width / viewport.height);
	float4x4 clip_from_world = mul4x4(clip_from_view, view_from_world);

	/* // clang-format off */
	/* float3 camera_right, camera_up; */
	/* float3 camera_forward = orthobasis3(sub3(camera->target, camera->position), &camera_right, &camera_up); */

	/* float4x4 world_from_view = {{ */
	/* [0] = camera_right.x, [4] = camera_up.x, [8 ] = -camera_forward.x, [12] = camera->position.x, */
	/* [1] = camera_right.y, [5] = camera_up.y, [9 ] = -camera_forward.y, [13] = camera->position.y, */
	/* [2] = camera_right.z, [6] = camera_up.z, [10] = -camera_forward.z, [14] = camera->position.z, */
	/* [3] = 0.0f,           [7] = 0.0f,        [11] =  0.0f,             [15] = 1.0f, */
	/* }}; */

	/* float aspect = viewport.width / viewport.height; */
	/* float f = tanf(deg_to_rad(camera->fovy) * 0.5f); */

	/* float4x4 view_from_clip = {{ */
	/* [0] = f * aspect, [4] = 0.0f, [8 ] = 0.0f, [12] = 0.0f, */
	/* [1] = 0.0f,       [5] = f,    [9 ] = 0.0f, [13] = 0.0f, */
	/* [2] = 0.0f,       [6] = 0.0f, [10] = 0.0f, [14] = 0.0f, */
	/* [3] = 0.0f,       [7] = 0.0f, [11] = -1.0f, [15] = 0.0f, */
	/* }}; */
	/* // clang-format on */

	/* int32_t seg_count = 32; */
	/* for (int32_t z = -seg_count; z <= seg_count; ++z) */
	/* draw3d_line(f3(-seg_count, 0.0f, z), f3(seg_count, 0.0f, z), 1.0f, z == 0 ? RED : GRAY); */
	/* for (int32_t x = -seg_count; x <= seg_count; ++x) */
	/* draw3d_line(f3(x, 0.0f, -seg_count), f3(x, 0.0f, seg_count), 1.0f, x == 0 ? GREEN : GRAY); */

	/* float3 point = { -1.0f, 0.0f, -8.0f }; */

	/* float fovy = 45.0f; */
	/* float2 frustum_near_plane_half_extent = { */
	/* 0.0, */
	/* tanf(deg_to_rad(fovy) * 0.5f), */
	/* }; */
	/* frustum_near_plane_half_extent.x = frustum_near_plane_half_extent.y * aspect; */

	/* // proj = scale3(point, plane.distance / dot3(plane.normal, point)); */
	/* float3 proj = { */
	/* point.x / (-point.z), */
	/* point.y / (-point.z), */
	/* -1.0f */
	/* }; */

	/* draw3d_arrow(f3(0.0f), proj, 4.0f, GREEN, view_from_world, clip_from_view, viewport.width); */
	/* draw3d_quad_outline((Plane){ { 0.0f, 0.0f, -1.0f }, 1.0f }, frustum_near_plane_half_extent.x * 2.0f, frustum_near_plane_half_extent.y * 2.0f, 4.0f, BLACK); */

	/* float2 viewport_size_inverse = f2(1.0f / (viewport.width * 0.5f), 1.0f / (viewport.height * 0.5f)); */
	/* float2 mouse_ndc = sub2(mul2(mouse_position, viewport_size_inverse), f2(1.0f)); */

	/* float near_plane_half_height = tanf(deg_to_rad(camera->fovy) * 0.5f); */
	/* float near_plane_half_width = near_plane_half_height * aspect; */

	/* float3 mouse_view = f3(mul2(mouse_ndc, f2(near_plane_half_width, near_plane_half_height)), -1.0f); */

	/* Ray3 mouse_ray = { */
	/* camera->position, */
	/* norm3(xform3v(world_from_view, mouse_view)), */
	/* }; */

	/* CastResult3 hit = raycast_plane(mouse_ray, (Plane){ .normal = unit3(UP), .distance = 0.0f }); */
	/* if (hit.hit) */
	/* draw3d_quad(hit.point, f2(1.0f), */
	/* (DRAW_QuadStyle){ */
	/* .fill_color = RED, */
	/* .radii = f4(1.0f), */
	/* .flags = 1, */
	/* }); */

	/* draw3d_quad(point, f2(0.1f), */
	/* (DRAW_QuadStyle){ */
	/* .fill_color = BLACK, */
	/* .radii = f4(1.0f), */
	/* .flags = 1, */
	/* }); */

	float point_size = 0.03f;
	static float segment_size = 64.0f;

	float2 origo = rect_center(viewport);
	float2 scale = { segment_size, -segment_size };

	int2 counts = {
		(int32_t)ceilf(viewport.width * 0.5f / segment_size),
		(int32_t)ceilf(viewport.height * 0.5f / segment_size)
	};

	// clang-format off
	float3x3 screen_from_world = {{ 
	[0] = scale.x, [3] = 0.0f ,   [6] = origo.x,
	[1] = 0.0f,    [4] = scale.y, [7] = origo.y,
	[2] = 0.0f,    [5] = 0.0f,    [8] = 1.0f
	}};

	float3x3 world_from_screen = {{
	[0] = 1.0f / scale.x, [3] = 0.0f,           [6] = -origo.x / scale.x,
	[1] = 0.0f,           [4] = 1.0f / scale.y, [7] = -origo.y / scale.y,
	[2] = 0.0f,           [5] = 0.0f,           [8] =  1.0f,
	}};
	// clang-format on

	for (int32_t x = -counts.x; x <= counts.x; ++x) {
		float px = origo.x + x * segment_size;

		Color c = x == 0 ? BLUE : GRAY;
		draw2d_line(make2(px, 0.0f), make2(px, viewport.height), 1.0f, c);
	}

	for (int32_t y = -counts.y; y <= counts.y; ++y) {
		float py = origo.y + y * segment_size;

		Color c = y == 0 ? RED : GRAY;
		draw2d_line(make2(0.0f, py), make2(viewport.width, py), 1.0f, c);
	}

	static Drag2D anchors[2] = { 0 };
	static Drag2D controls[2] = { 0 };
	drag2d_point(&anchors[0], f2(-2.0f, -1.0f), 0.15f, xform2p(world_from_screen, mouse_position));
	drag2d_point(&anchors[1], f2(1.0f, 2.0f), 0.15f, xform2p(world_from_screen, mouse_position));

	drag2d_point(&controls[0], f2(-3.0f, 0.0f), 0.1f, xform2p(world_from_screen, mouse_position));
	drag2d_point(&controls[1], f2(0.0f, 3.0f), 0.1f, xform2p(world_from_screen, mouse_position));

	draw2d_line(xform2p(screen_from_world, anchors[0].position), xform2p(screen_from_world, controls[0].position), 4.0f, BLACK);
	draw2d_line(xform2p(screen_from_world, anchors[1].position), xform2p(screen_from_world, controls[1].position), 4.1f, BLACK);

	float step = 1.0f / 128.0f;
	for (uint32_t res = 0; res < 128; ++res) {
		float t = (float)res / 128.0f;

		float2 a = lerp2(anchors[0].position, controls[0].position, t);
		float2 b = lerp2(controls[0].position, anchors[1].position, t);
		float2 q1 = lerp2(a, b, t);

		float2 c = lerp2(anchors[0].position, controls[1].position, t);
		float2 d = lerp2(controls[1].position, anchors[1].position, t);
		float2 q2 = lerp2(c, d, t);

		float2 cubic = lerp2(q1, q2, t);

		draw2d_circle(xform2p(screen_from_world, cubic), 2.0f, BLACK);
	}

	for (uint32_t index = 0; index < countof(anchors); ++index)
		draw2d_circle(xform2p(screen_from_world, anchors[index].position), segment_size * 0.15f, RED);

	for (uint32_t index = 0; index < countof(controls); ++index)
		draw2d_circle(xform2p(screen_from_world, controls[index].position), segment_size * 0.1f, BLACK);

	/*
	auto brick = Spatial::material({
		.albedo = textures::brick_albedo,
		.normal = textures::brick_normal,
		.metallic_roughness = { 0.0f, 0.8f },
	});

	draw3d_mesh(mesh, transform, brick);

	auto material = Unlit::material({
		.color = logo,
	});

	draw3d_mesh(quad, transform, material);
	   */
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
		GFX_Image *images[32] = { 0 };
		uint32_t image_count = 1;
		for (uint32_t texture_id = 0; texture_id < 32; ++texture_id)
			images[texture_id] = state->white_texture;

		if (draw->quad2d->offset) {
			uint32_t quad_count = draw->quad2d->offset / sizeof(DRAW_Quad2D);
			for (uint32_t quad_instance = 0; quad_instance < quad_count; ++quad_instance) {
				DRAW_Quad2D *quad = (DRAW_Quad2D *)draw->quad2d->base + quad_instance;

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
		}

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
			.view = view_from_world,
			.proj = clip_from_view,
			.camera_position = make4_from3(state->camera.position, 0.0f),
		};
		Uniform set0[] = { uniform_data(0, &fd, sizeof(fd)) };
		gfx_cmd_bind(device, 0, set0, countof(set0));

		if (draw->line3d->offset) {
			gfx_cmd_shader_bind(device, state->shaders[RES_SHADER_LINE3D], 0);

			Uniform set1[] = {
				storage_data(0, draw->line3d->base, draw->line3d->offset),
			};
			gfx_cmd_bind(device, 0, set0, countof(set0));
			gfx_cmd_bind(device, 1, set1, countof(set1));
			gfx_cmd_draw_instanced(cmd, 0, 6, 0, draw->line3d->offset / sizeof(DRAW_Line3D));
		}

		if (draw->quad2d->offset) {
			FrameData fd = {
				.view = identity4x4(),
				.proj = orthographic(0.0f, dims.x, 0.0f, dims.y, -50.f, 50.f),
				.viewport = as2(dims, float2),
				.time = time,
			};
			uint32_t quad_count = draw->quad2d->offset / sizeof(DRAW_Quad2D);

			gfx_cmd_shader_bind(device, state->shaders[RES_SHADER_QUAD2D], 0);

			Uniform uniforms0[] = {
				uniform_data(0, &fd, sizeof(fd)),
				storage_data(1, draw->quad2d->base, draw->quad2d->offset),
			};
			Uniform uniforms1[] = { sampler_with_textures(0, images, countof(images), state->nearest) };

			gfx_cmd_bind(device, 0, uniforms0, countof(uniforms0));
			gfx_cmd_bind(device, 1, uniforms1, countof(uniforms1));

			gfx_cmd_draw_instanced(cmd, 0, 6, 0, quad_count);
		}

		gfx_cmd_draw_end(cmd);
	}

	gfx_frame_end(device, cmd);

	for (RES_ShaderID shaderid = 0; shaderid < RES_SHADER_MAX; ++shaderid) { // :hot-reload
		ShaderMetadata *metadata = &res_shaderid_to_metadata[shaderid];
		if (metadata->filepaths[SHADER_STAGE_VERTEX].length == 0 &&
			metadata->filepaths[SHADER_STAGE_FRAGMENT].length == 0 &&
			metadata->filepaths[SHADER_STAGE_COMPUTE].length == 0)
			continue;

		bool is_compute = metadata->filepaths[SHADER_STAGE_COMPUTE].length > 0;
		if (is_compute) {
			OS_Timestamp now = os_file_mtime(metadata->filepaths[SHADER_STAGE_COMPUTE]);

			if (now != state->shader_ts[shaderid]) {
				LOG_INFO("hot-reloading %s...", state->shaders[shaderid]->debug_name);
				gfx_device_wait_idle(device);

				gfx_shader_destroy(device, state->shaders[shaderid]);
				string8 bytecode = os_file_read(frame, metadata->filepaths[SHADER_STAGE_COMPUTE]);
				state->shaders[shaderid] = gfx_compute_make(device, bytecode, (char *)metadata->name.bytes);

				state->shader_ts[shaderid] = now;
			}
		} else {
			OS_Timestamp fs_ts = os_file_mtime(metadata->filepaths[SHADER_STAGE_FRAGMENT]);
			OS_Timestamp vs_ts = os_file_mtime(metadata->filepaths[SHADER_STAGE_VERTEX]);

			OS_Timestamp now = MAX(fs_ts, vs_ts);
			if (now != state->shader_ts[shaderid]) {
				LOG_INFO("hot-reloading %s...", state->shaders[shaderid]->debug_name);
				gfx_device_wait_idle(device);

				ShaderMetadata *metadata = &res_shaderid_to_metadata[shaderid];

				gfx_shader_destroy(device, state->shaders[shaderid]);

				string8 vs_bytecode = os_file_read(frame, metadata->filepaths[SHADER_STAGE_VERTEX]);
				string8 fs_bytecode = os_file_read(frame, metadata->filepaths[SHADER_STAGE_FRAGMENT]);
				state->shaders[shaderid] = gfx_shader_make(device, vs_bytecode, fs_bytecode, (char *)metadata->name.bytes);

				for (uint32_t permutation = 0; permutation < metadata->pipeline_count; ++permutation) {
					PipelineOptions opts = metadata->pipelines[permutation];
					gfx_pipeline_ensure(device, state->shaders[shaderid], opts,
						(GFX_DrawTargetLayout){
						  .color_formats[0] = PIXEL_FORMAT_BGRA8_UNORM,
						  .color_count = 1,
						});
				}

				state->shader_ts[shaderid] = now;
			}
		}
	}

	return true;
}
