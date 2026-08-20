#include <core.h>

#include "common.h"
#include "core/arena.h"
#include "core/logger.h"

#include "draw.h"
#include "gfx.h"
#include "gfx/gfx_types.h"

#include "os.h"
#include "res/tables.h"

void load_or_reload_shader(GFX_Device *device, ShaderID id, GFX_Shader *shaders[SHADER_MAX], OS_Timestamp ts[SHADER_MAX]) {
	ShaderMetadata *metadata = &shader_to_metadata[id];
	if (metadata->filepaths[SHADER_STAGE_VERTEX].length == 0 &&
		metadata->filepaths[SHADER_STAGE_FRAGMENT].length == 0 &&
		metadata->filepaths[SHADER_STAGE_COMPUTE].length == 0)
		return;

	ArenaTemp scratch = arena_scratch_begin(NULL);
	bool is_compute = metadata->filepaths[SHADER_STAGE_COMPUTE].length > 0;
	if (is_compute) {
		ts[id] = os_file_last_modified(metadata->filepaths[SHADER_STAGE_COMPUTE]);
		String8 bytecode = os_file_read_entire(scratch.arena, metadata->filepaths[SHADER_STAGE_COMPUTE]);
		shaders[id] = gfx_compute_make(device, bytecode, (char *)shader_to_string[id].text);
	} else {
		String8 vs_bytecode = os_file_read_entire(scratch.arena, metadata->filepaths[SHADER_STAGE_VERTEX]);
		String8 fs_bytecode = os_file_read_entire(scratch.arena, metadata->filepaths[SHADER_STAGE_FRAGMENT]);

		OS_Timestamp fs_ts = os_file_last_modified(metadata->filepaths[SHADER_STAGE_FRAGMENT]);
		OS_Timestamp vs_ts = os_file_last_modified(metadata->filepaths[SHADER_STAGE_VERTEX]);

		ts[id] = MAX(fs_ts, vs_ts);
		shaders[id] = gfx_shader_make(device, vs_bytecode, fs_bytecode, (char *)shader_to_string[id].text);
		for (uint32_t permutation = 0; permutation < metadata->pipeline_count; ++permutation)
			gfx_pipeline_ensure(device, shaders[id], metadata->pipelines[permutation]);
	}

	arena_scratch_end(scratch);
}

int main(void) {
	logger_set_level(LOG_LEVEL_DEBUG);
	os_display_startup();

	uint64_t start_time = os_time_ns();

	GFX_Device device[] = { 0 };
	gfx_device_make(device);

	OS_Surface *window = os_surface_open(1280, 720, s("Draw Scratch"), OS_SURFACE_FLAG_RESIZEABLE);
	GFX_Swapchain *swapchain = gfx_swapchain_make(device, window, "window");

	OS_Timestamp ts[SHADER_MAX];
	GFX_Shader *shaders[SHADER_MAX] = { 0 };
	for (uint32_t index = 0; index < SHADER_MAX; ++index)
		load_or_reload_shader(device, index, shaders, ts);

	GFX_Image *target = gfx_image_make(device, 1280, 720,
		(ImageOptions){
		  .debug_name = "target:color",
		  .format = shader_to_metadata[SHADER_QUAD2D].pipelines[0].color_attachments[0],
		  .usage = IMAGE_USAGE_RENDER | IMAGE_USAGE_TRANSFER,
		});

	GFX_Image *default_texture = gfx_image_make(device, 1, 1, (ImageOptions){ .pixels = (uint32_t[]){ 0xFFFFFFFF } });
	GFX_Sampler *nearest = gfx_sampler_make(device, sampler_opt("nearest:clamp_border", FILTER_NEAREST, WRAP_MODE_CLAMP_BORDER));

	Arena batch2d = arena_make(MiB(32));
	for (bool is_open = true; is_open;) {
		float time = (os_time_ns() * 1e-9) - (start_time * 1e-9);

		uint2 resize = { 0 };
		for (OS_Event event; os_event_poll(&event);) {
			if (event.type == OS_EVENT_TYPE_SURFACE_CLOSE)
				is_open = false;
			else if (event.type == OS_EVENT_TYPE_SURFACE_RESIZE) {
				resize.x = event.as.resize.width, resize.y = event.as.resize.height;
			}
		}
		uint2 dims = os_surface_size(window);
		if (resize.x && resize.y) {
			gfx_swapchain_resize(device, swapchain, resize.x, resize.y);
			gfx_image_resize(device, target, resize.x, resize.y);
		}

		draw2d_rect(&batch2d, rect(100.0f, 100.0f, 200.0f, 300.0f), RED);

		GFX_Command *cmd = gfx_frame_begin(device);
		if (cmd == 0) break;

		GFX_Image *backbuffer = gfx_backbuffer(device, cmd, swapchain);
		if (target) {
			gfx_cmd_draw_begin(cmd,
				(GFX_DrawPassInfo){
				  .debug_name = "Pass2D",
				  .colors[0] = {
					.target = target,
					.load = LOAD_OP_CLEAR,
					.clear = WHITE,
				  },
				});

			if (batch2d.offset) {
				gfx_cmd_shader_bind(cmd, shaders[SHADER_QUAD2D]);
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
					storage_data(1, batch2d.base, batch2d.offset),
				};

				GFX_Image *images[32] = { 0 };
				for (uint32_t index = 0; index < countof(images); ++index)
					images[index] = default_texture;
				Uniform uniforms1[] = { sampler_with_textures(0, images, countof(images), nearest) };

				gfx_cmd_bind(device, 0, uniforms0, countof(uniforms0));
				gfx_cmd_bind(device, 1, uniforms1, countof(uniforms1));

				uint32_t vertex_count = batch2d.offset / sizeof(QuadVertex2D);
				gfx_cmd_draw(cmd, vertex_count, 0);
			}
			gfx_cmd_draw_end(cmd);

			gfx_cmd_image_blit(cmd, (Rectangle){ 0 }, target, (Rectangle){ 0 }, backbuffer);
		}

		gfx_frame_end(device, cmd);

		arena_reset(&batch2d);
	}

	gfx_device_destroy(device);
	os_display_shutdown();

	return 0;
}
