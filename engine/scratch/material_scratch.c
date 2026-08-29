#include "common.h"
#include "core/arena.h"
#include "gfx.h"
#include "gfx/gfx_types.h"
#include "os.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

int main(void) {
	logger_set_level(LOG_LEVEL_DEBUG);

	os_display_startup();
	OS_Surface *window = os_surface_open(1280, 720, s("material_scratch"), OS_SURFACE_FLAG_RESIZEABLE);

	GFX_Device device[] = { 0 };
	gfx_device_make(device);

	GFX_Swapchain *swapchain = gfx_swapchain_make(device, window, "win");
	GFX_Image *white_texture = gfx_image_make(device, 1, 1,
		(ImageOptions){
		  .debug_name = "default:white",
		  .usage = IMAGE_USAGE_TRANSFER | IMAGE_USAGE_SAMPLE,
		  .pixels = (uint8_t[]){ 255, 255, 255, 255 },
		});
	GFX_Sampler *nearest = gfx_sampler_make(device,
		sampler_opt("default:nearest", FILTER_NEAREST, WRAP_MODE_CLAMP));

	ArenaTemp scratch = arena_scratch_begin(0);
	GFX_Shader *quad2d = gfx_shader_make(device,
		os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/batch2d.vertex.spv")),
		os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/quad.fragment.spv")),
		"shader:quad2d");
	gfx_pipeline_ensure(device, quad2d,
		(PipelineOptions){
		  .color_attachments = { PIXEL_FORMAT_BGRA8_UNORM },
		  .color_attachment_count = 1,
		  .enable_blend = true,
		  .src_color_factor = BLEND_FACTOR_SRC_ALPHA,
		  .dst_color_factor = BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		  .src_alpha_factor = BLEND_FACTOR_ONE,
		  .dst_alpha_factor = BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,

		  .disable_depth_test = true,
		  .disable_depth_write = true,
		});

	arena_scratch_end(scratch);

	for (bool is_open = true; is_open;) {
		for (OS_Event event; os_event_poll(&event);) {
			switch (event.type) {
				case OS_EVENT_TYPE_SURFACE_CLOSE:
					is_open = false;
					break;
				case OS_EVENT_TYPE_SURFACE_RESIZE:
					gfx_swapchain_resize(device, swapchain, event.as.resize.width, event.as.resize.height);
					break;

				case OS_EVENT_TYPE_KEY_PRESS:
				case OS_EVENT_TYPE_KEY_RELEASE:
					break;
				default:
					break;
			}
		}

		uint2 dims = os_surface_size(window);
		Rectangle viewport = { 0, 0, dims.x, dims.y };
		float time = os_time_ns() * 1e-9;

		GFX_Command *cmd = gfx_frame_begin(device);
		if (cmd == 0) continue;

		GFX_Image *screen = gfx_swapchain_backbuffer(device, cmd, swapchain);
		if (screen) {
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_COLOR_ATTACHMENT, screen);

			VkRenderingAttachmentInfo color_attachments[] = {
				[0] = {
				  .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				  .clearValue.color.float32 = { 1.0f, 0.0f, 1.0f, 1.0f },
				  .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				  .imageView = screen->view,
				  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				},
			};

			VkRenderingInfo info = {
				.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
				.colorAttachmentCount = countof(color_attachments),
				.pColorAttachments = color_attachments,
				.renderArea = {
				  .extent = { viewport.width, viewport.height },
				},
				.layerCount = 1,
			};
			vkCmdBeginRendering(cmd->handle, &info);

			VkViewport viewports[] = {
				[0] = { .width = viewport.width, .height = viewport.height },
			};
			VkRect2D scissors[] = {
				[0] = { .extent = { viewport.width, viewport.height } },
			};

			vkCmdSetViewport(cmd->handle, 0, 1, viewports);
			vkCmdSetScissor(cmd->handle, 0, 1, scissors);

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

			typedef struct {
				float2 position, uv;
				float4 radii;
				float2 size;
				uint32_t fill_color, border_color;
				float border_width;
				uint32_t imageid;
			} QuadVertex2D;

			float s = 180.0f;
			float2 pos = splat2((viewport.width - s) * 0.5f);

			float2 corners[] = {
				{ pos.x, pos.y },
				{ pos.x + s, pos.y },
				{ pos.x, pos.y + s },
				{ pos.x + s, pos.y + s },
			};
			float2 uvs[] = {
				{ 0 },
				{ 1.0f, 0.0f },
				{ 0.0f, 1.0f },
				{ 1.0f, 1.0f },
			};

			Color c = TEAL;
			float4 radii = splat4(8.0f);

			QuadVertex2D vertices[6] = {
				[0] = { corners[0], uvs[0], radii, { viewport.width, viewport.height }, color_pack_uint32(c), color_pack_uint32(TRANSPARENT), 0.0f, 0 },
				[1] = { corners[2], uvs[2], radii, { viewport.width, viewport.height }, color_pack_uint32(c), color_pack_uint32(TRANSPARENT), 0.0f, 0 },
				[2] = { corners[3], uvs[3], radii, { viewport.width, viewport.height }, color_pack_uint32(c), color_pack_uint32(TRANSPARENT), 0.0f, 0 },

				[3] = { corners[0], uvs[0], radii, { viewport.width, viewport.height }, color_pack_uint32(c), color_pack_uint32(TRANSPARENT), 0.0f, 0 },
				[4] = { corners[3], uvs[3], radii, { viewport.width, viewport.height }, color_pack_uint32(c), color_pack_uint32(TRANSPARENT), 0.0f, 0 },
				[5] = { corners[1], uvs[1], radii, { viewport.width, viewport.height }, color_pack_uint32(c), color_pack_uint32(TRANSPARENT), 0.0f, 0 }
			};

			Uniform uniforms0[] = {
				uniform_data(0, &frame_2d, sizeof(frame_2d)),
				storage_data(1, vertices, sizeof(vertices)),
			};

			GFX_Image *images[32] = { 0 };
			for (uint32_t index = 0; index < countof(images); ++index)
				images[index] = white_texture;

			Uniform uniforms1[] = { sampler_with_textures(0, images, countof(images), nearest) };

			gfx_cmd_shader_bind(cmd, quad2d);
			gfx_cmd_bind(device, 0, uniforms0, countof(uniforms0));
			gfx_cmd_bind(device, 1, uniforms1, countof(uniforms1));

			vkCmdDraw(cmd->handle, 6, 1, 0, 0);

			vkCmdEndRendering(cmd->handle);
		}

		gfx_frame_end(device, cmd);
	}
	gfx_device_destroy(device);
	os_display_shutdown();

	return 0;
}
