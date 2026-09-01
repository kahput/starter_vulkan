#include "app/scene.h"
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
#include <draw/imgui.h>

#include <utils/input.h>
#include <utils/anim.h>

typedef struct {
	Arena *permanent, *frame;
	GFX_Device device[1];

	OS_Surface *surface;
	GFX_Swapchain *swapchain;

	InputState input;
	GFX_Shader *shaders[SHADER_MAX];

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

bool tick(Arena *permanent, Arena *frame) {
	state = (State *)permanent->base;
	input_set_context(&state->input);

	if (state->initialized == false) {
		arena_push_count(permanent, State, 1); // reserve space for state
		state->permanent = permanent, state->frame = frame;

		gfx_device_make(state->device);

		state->surface = os_surface_open(1280, 720, str8_wrap(named("game")), OS_SURFACE_FLAG_RESIZEABLE);
		state->swapchain = gfx_swapchain_make(state->device, state->surface, named("main"));

		for (uint32_t index = 0; index < SHADER_MAX; ++index) {
			ShaderMetadata *metadata = &shaderid_to_metadata[index];

			bool is_compute = metadata->filepaths[SHADER_STAGE_COMPUTE].length;
			if (is_compute) {
			} else {
				String8 vs = os_file_read_entire(frame, metadata->filepaths[SHADER_STAGE_VERTEX]);
				String8 fs = os_file_read_entire(frame, metadata->filepaths[SHADER_STAGE_FRAGMENT]);
				state->shaders[index] = gfx_shader_make(state->device, vs, fs, (char *)metadata->name.text);
				for (uint32_t permutation = 0; permutation < metadata->pipeline_count; ++permutation) {
					PipelineOptions opts = metadata->pipelines[permutation];
					opts.color_attachments[0] = PIXEL_FORMAT_BGRA8_UNORM;
					opts.sample_count = 1;
					gfx_pipeline_ensure(state->device, state->shaders[index], opts);
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

	float2 mouse_delta = cast2(input_mouse_delta(), float2);
	mouse_delta.x /= dims.x;
	mouse_delta.y /= dims.y;

	float4x4 view = camera_view(&state->camera);
	float4x4 proj = camera_proj(&state->camera, viewport.width / viewport.height);

	Arena line3d[] = { {
	  .base = arena_push_count(frame, DRAW_Line3D, 1024),
	  .capacity = sizeof(DRAW_Line3D) * 1024,
	} };
	Arena quad2d[] = { {
	  .base = arena_push_count(frame, DRAW_Quad2D, 1024),
	  .capacity = sizeof(DRAW_Quad2D) * 1024,
	} };

	Sphere sphere = {
		.center = splat3(0.0f),
		.radius = 8.0f,
	};

	float3 move = { 12.0f, 0.0f, 0.0f };

	float3 direction = norm3(make3(0.01f, 0.0f, 1.0f));
	float3 swept_support = shape3_furthest_point(&(Shape3){ .kind = SHAPE_KIND_SPHERE, .as.sphere = sphere }, direction);
	if (dot3(move, direction) > 0.0f)
		swept_support = add3(swept_support, move);

    draw3d_arrow(line3d, sphere.center, add3(sphere.center, scale3(direction, 3.0f)), 3.0f, BLACK, view, proj, viewport.width);
    draw3d_sphere_outline(line3d, swept_support, 3.0f, 32, 1.0f, RED);

	Capsule3 swept_sphere = {
		sphere.center,
		add3(sphere.center, move),
		sphere.radius,
	};

	Capsule3 capsule = {
		.a = { cosf(time * 2.0f) * 24.0f, -4.0f, 0.0f },
		.b = { cosf(time * 2.0f) * 24.0f, 4.0f, 0.0f },
		.radius = 4.0f,
	};

	scene_camera_orbit(camera, mouse_delta);

	Shape3 a = shape3_from_capsule(swept_sphere);
	Shape3 b = shape3_from_capsule(capsule);

	Color c = gjk_overlap(&a, &b) ? RED : BLACK;

	draw3d_arrow(line3d, sphere.center, add3(sphere.center, scale3(norm3(move), 3.0f)), 3.0f, BLACK, view, proj, viewport.width);
	draw3d_capsule_outline(line3d, swept_sphere.a, swept_sphere.b, swept_sphere.radius, 32, 3.0f, BLUE);
	draw3d_capsule_outline(line3d, capsule.a, capsule.b, capsule.radius, 32, 3.0f, c);
	/* draw3d_sphere_outline(line3d, sphere.center, sphere.radius, 32, 4.0f, c); */

	draw3d_ellipsoid_outline(line3d, make3(20.0f, 0.0f, 20.0f), make3(4.0f, 8.0f, 4.0f), 32, 3.0f, RED);

	GFX_Device *device = state->device;
	GFX_Swapchain *swapchain = state->swapchain;

	if (resize.x && resize.y)
		gfx_swapchain_resize(device, swapchain, resize.x, resize.y);

	GFX_CommandEncoder *cmd = gfx_frame_begin(state->device);
	if (cmd == 0) return false;

	GFX_Image *backbuffer = gfx_swapchain_backbuffer(device, cmd, swapchain);
	if (backbuffer) {
		gfx_cmd_draw_begin(cmd,
			(GFX_DrawPassInfo){
			  .debug_name = "main",
			  .colors[0] = {
				.target = backbuffer,
				.load = LOAD_OP_CLEAR,
				.store = STORE_OP_STORE,
				.clear = WHITE,
			  },
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

		if (line3d->offset) {
			FrameData fd = {
				.viewport = { dims.x, dims.y },
				.time = time,
				.view = view,
				.proj = proj,
				.camera_position = make4_from3(state->camera.position, 0.0f),
			};

			gfx_cmd_shader_bind(cmd, state->shaders[SHADER_LINE3D]);

			Uniform uniforms[] = {
				uniform_data(0, &fd, sizeof(fd)),
			};
			gfx_cmd_bind(device, 0, uniforms, countof(uniforms));
			Uniform set1[] = {
				storage_data(0, line3d->base, line3d->offset),
			};
			gfx_cmd_bind(device, 1, set1, countof(set1));
			gfx_cmd_draw_instanced(cmd, 0, 6, 0, line3d->offset / sizeof(DRAW_Line3D));
		}

		if (quad2d->offset) {
			FrameData fd = {
				.view = identity4x4(),
				.proj = orthographic(0.0f, dims.x, 0.0f, dims.y, -50.f, 50.f),
				.viewport = cast2(dims, float2),
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
	return true;
}
