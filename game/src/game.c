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

	bool initialized;
} State;

// image imfilter(image, kernel, boundary_condition);
// rgb2gray(im
// normxcorr2(onion, peppers);
// image imedge(image, algorithm);

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

bool tick(Arena *permanent, Arena *frame) {
	if (permanent->offset == 0) arena_push_count(permanent, State, 1); // reserve space for state

	state = (State *)permanent->base;
	input_set_context(&state->input);

	if (state->initialized == false) {
		state->permanent = permanent, state->frame = frame;

		if (gfx_device_make(state->device) == false) return false;

		state->surface = os_surface_open(1280, 720, str8z(named("game")), OS_SURFACE_FLAG_RESIZEABLE);
		state->swapchain = gfx_swapchain_make(state->device, state->surface, named("main"));
		state->depthbuffer = gfx_image_make(state->device, 1280, 720,
			(ImageOptions){
			  .debug_name = named("target:main_depth"),
			  .format = PIXEL_FORMAT_DEPTH,
			  .usage = IMAGE_USAGE_RENDER,
			});

		state->white_texture = gfx_image_make(state->device, 1, 1, (ImageOptions){ .pixels = (uint8_t[]){ 255, 255, 255, 255 } });
		state->sampler_nearest = gfx_sampler_make(state->device, sampler_opt(named("sampler:nearest"), FILTER_NEAREST, WRAP_MODE_CLAMP));
		state->sampler_linear = gfx_sampler_make(state->device, sampler_opt(named("sampler:linear"), FILTER_LINEAR, WRAP_MODE_CLAMP));

		Arena *resource_arena = arena_push_count(permanent, Arena, 1);
		resource_arena->base = arena_push(state->permanent, MiB(8), alignof(long double), true);
		resource_arena->capacity = MiB(8);

		state->cache = res_cache_make(resource_arena, state->device, &registry);

		state->camera = (Camera){
			.projection = CAMERA_PROJECTION_PERSPECTIVE,
			.position = { 0.0f, 3.0f, 8.f },
			.target = { 0.0f, 1.5f, 0.0f },
			.up = FLOAT3_UP,
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

	DRAW_List *draw = drawlist_make(frame, RES_SHADER_QUAD2D, RES_SHADER_LINE3D);
	scene_camera_orbit(camera, mouse_delta);

	float3 camera_forward = norm3(sub3(camera->target, camera->position));
	float3 camera_right = norm3(cross3(camera_forward, FLOAT3_UP));
	float3 camera_up = cross3(camera_right, camera_forward);

	float3x3 camera_rotation = columns3x3(camera_right, camera_up, neg3(camera_forward));
	float4x4 world_from_view = affine4x4(
		camera_rotation,
		camera->position //
	);

	float4x4 view_from_world = affine4x4(
		transpose3x3(camera_rotation),
		float3(-dot3(camera_right, camera->position), -dot3(camera_up, camera->position), dot3(camera_forward, camera->position)) //
	);

	float4x4 clip_from_view = camera_proj(&state->camera, viewport.width / viewport.height);
	float4x4 clip_from_world = mul4x4(clip_from_view, view_from_world);

	int32_t seg_count = 32;
	for (int32_t z = -seg_count; z <= seg_count; ++z)
		draw3d_line(float3(-seg_count, 0.0f, z), float3(seg_count, 0.0f, z), 1.0f, z == 0 ? RED : GRAY);
	for (int32_t x = -seg_count; x <= seg_count; ++x)
		draw3d_line(float3(x, 0.0f, -seg_count), float3(x, 0.0f, seg_count), 1.0f, x == 0 ? GREEN : GRAY);

	float4x4 world_from_local = affine4x4(identity3x3(), FLOAT3_UP);

	static Drag2D slider = { 0 };

	uint32_t segments = 32, rings = 16;
	float radius = 0.5f, height = 2.0f;

	Mesh mesh = mesh_capsule(frame, float3(0.0f), 2.0f, 0.5f, 32, 16);

	GFX_Device *device = state->device;
	GFX_Swapchain *swapchain = state->swapchain;

	if (resize.x && resize.y) {
		gfx_swapchain_resize(device, swapchain, resize.x, resize.y);
		gfx_image_resize(device, state->depthbuffer, resize.x, resize.y);
	}
	res_cache_tick(frame, &state->cache);

	GFX_CommandEncoder *cmd = gfx_frame_begin(state->device);
	if (cmd == 0) return false;

	GFX_Image *backbuffer = gfx_swapchain_backbuffer(device, cmd, swapchain);
	if (backbuffer) {
		gfx_cmd_draw_begin(cmd,
			(GFX_DrawPassInfo){
			  .debug_name = "main",
			  .colors[0] = { .target = backbuffer, .load = LOAD_OP_CLEAR, .store = STORE_OP_STORE, .clear = hex(0x101010) },
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

		if (draw->line3d.instances.offset) {
			RES_Shader *line3d = res_shader(&state->cache, RES_SHADER_LINE3D);
			ASSERT(line3d);
			gfx_cmd_shader_bind(device, line3d->handle, 0);

			Uniform set1[] = {
				storage_data(0, draw->line3d.instances.base, draw->line3d.instances.offset),
			};
			Uniform set0[] = { uniform_data(0, &fd, sizeof(fd)) };
			gfx_cmd_bind(device, 0, set0, countof(set0));
			gfx_cmd_bind(device, 0, set0, countof(set0));
			gfx_cmd_bind(device, 1, set1, countof(set1));
			gfx_cmd_draw_instanced(cmd, 0, 6, 0, draw->line3d.instances.offset / sizeof(DRAW_LineInstance3D));
		}

		{
			RES_Shader *unlit = res_shader(&state->cache, RES_SHADER_UNLIT);
			gfx_cmd_shader_bind(device, unlit->handle, 0);

			VertexLayout shader_vlayout = vertex_layout(
				{ VERTEX_SEMANTIC_POSITION, DATA_FORMAT_FLOAT32x4 },
				{ VERTEX_SEMANTIC_NORMAL, DATA_FORMAT_FLOAT32x4 },
				{ VERTEX_SEMANTIC_UV0, DATA_FORMAT_FLOAT32x2 },
				{ VERTEX_SEMANTIC_UV1, DATA_FORMAT_FLOAT32x2 } //
			);

			uint64_t vertices_size = mesh.total_vertex_count * shader_vlayout.stride;

			uint64_t offset = alignup(cmd->transient_arena->offset, 256);
			void *vertices = arena_push(cmd->transient_arena, alignup(vertices_size, 256), 256, 0);
			mesh_pack_into(&mesh, &shader_vlayout, vertices, vertices_size);

			Uniform set0[] = { uniform_data(0, &fd, sizeof(fd)) };
			Uniform set1[] = { storage_buffers(0, cmd->transient_buffer, offset, vertices_size) };

			gfx_cmd_bind(device, 0, set0, countof(set0));
			gfx_cmd_bind(device, 1, set1, countof(set0));

			gfx_cmd_push_constant(cmd, sizeof(world_from_local), world_from_local.elements);

			uint64_t index_offset = gfx_cmd_put(cmd, mesh.total_index_count * index_format_to_size(mesh.index_format), mesh.indices);
			if (mesh.index_format == INDEX_FORMAT32)
				gfx_cmd_bind_index_buffer32(cmd, cmd->transient_buffer, index_offset);
			else
				gfx_cmd_bind_index_buffer16(cmd, cmd->transient_buffer, index_offset);

			for (uint32_t part_index = 0; part_index < mesh_part_count(mesh); ++part_index) {
				MeshPart part = meshpart(mesh, part_index);

				if (part.index_count) {
					gfx_cmd_draw_indexed(cmd, part.index_offset, part.index_count, part.vertex_offset);
				} else {
					gfx_cmd_draw(cmd, part.vertex_offset, part.vertex_count);
				}
			}
		}

		{
			DRAW_Channel *qch = &draw->quad2d;
			FrameData fd = {
				.view = identity4x4(),
				.proj = orthographic(0.0f, dims.x, 0.0f, dims.y, -50.f, 50.f),
				.viewport = as2(dims, float2),
				.time = time,
			};

			for (DRAW_Batch *batch = qch->first_batch; batch; batch = batch->next) {
				RES_Shader *shader = res_shader(&state->cache, batch->shader);
				gfx_cmd_shader_bind(device, shader->handle, 0);

				GFX_Image *images[32] = { 0 };
				uint32_t image_count = 1;
				for (uint32_t i = 0; i < 32; ++i)
					images[i] = state->white_texture;

				DRAW_QuadInstance3D *base =
					(DRAW_QuadInstance3D *)((uint8_t *)qch->instances.base + batch->instance_offset);

				for (uint32_t qi = 0; qi < batch->instance_count; ++qi) {
					DRAW_QuadInstance3D *quad = base + qi;
					if (quad->imageid && quad->imageid != indexof(device->image_pool, state->white_texture)) {
						int32_t found_index = -1;
						for (uint32_t ii = 1; ii < image_count; ++ii)
							if (indexof(device->image_pool, images[ii]) == quad->imageid) {
								found_index = ii;
								break;
							}
						if (found_index == -1) {
							ASSERT(image_count < countof(images));
							found_index = image_count++;
							images[found_index] = &device->image_pool[quad->imageid];
							gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, device->image_pool + quad->imageid);
						}
						quad->imageid = found_index;
					}
				}

				Uniform uniforms0[] = {
					uniform_data(0, &fd, sizeof(fd)),
					storage_data(1, base, batch->instance_count * sizeof(DRAW_QuadInstance3D)),
				};
				Uniform uniforms1[] = { sampler_with_textures(0, images, countof(images), state->sampler_nearest) };
				gfx_cmd_bind(device, 0, uniforms0, countof(uniforms0));
				gfx_cmd_bind(device, 1, uniforms1, countof(uniforms1));

				gfx_cmd_draw_instanced(cmd, 0, 6, 0, batch->instance_count);
			}
		}

		gfx_cmd_draw_end(cmd);
	}

	gfx_frame_end(device, cmd);

	return true;
}
