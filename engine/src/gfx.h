#pragma once

#include "core/arena.h"

#include "gfx/gfx_types.h"
#if 1
	#include "core/shape2.h"
#endif

static inline bool gfx_image_valid(GFX_Context *context, GFX_Image *image) {
	return context && image && image >= context->image_pool && image < context->image_pool + MAX_IMAGES;
}

bool gfx_startup(GFX_Context *context);
void gfx_shutdown(GFX_Context *context);

GFX_CommandContext *gfx_frame_begin(GFX_Context *context);
bool gfx_frame_end(GFX_Context *context, GFX_CommandContext *cmd);

GFX_CommandContext *gfx_transfer_cmd(GFX_Context *context);
bool gfx_transfer_flush(GFX_Context *context);

GFX_Buffer *gfx_buffer_make(GFX_Context *context, uint64_t size, BufferOptions options);
GFX_Image *gfx_image_make(GFX_Context *context, uint32_t width, uint32_t height, ImageOptions options);
GFX_Sampler *gfx_sampler_make(GFX_Context *context, SamplerOptions opt);
GFX_Swapchain *gfx_swapchain_make(GFX_Context *context, OS_Surface *surface, const char *debug_name);

bool gfx_reflect_shader_uniforms(String8 bytecode, UniformSet out_sets[GFX_LIMIT_UNIFORM_SETS]);
GFX_Pipeline compute_pipeline_make(GFX_Context *context, String8 bytecode);
GFX_Pipeline graphics_pipeline_make(GFX_Context *context, String8 vs_bytecode, String8 fs_bytecode, PipelineOptions options);

bool gfx_buffer_destroy(GFX_Context *context, GFX_Buffer *buffer);
bool gfx_image_destroy(GFX_Context *context, GFX_Image *image);
bool gfx_sampler_destroy(GFX_Context *context, GFX_Sampler *sampler);
bool gfx_swapchain_destroy(GFX_Context *context, GFX_Swapchain *surface);
void pipeline_destroy(GFX_Context *context, GFX_Pipeline *pipeline);

GFX_Image *gfx_backbuffer(GFX_Context *context, GFX_CommandContext *cmd, GFX_Swapchain *swapchain);
void gfx_present(GFX_Context *context, GFX_Swapchain *swapchain, GFX_Image *image);
bool gfx_swapchain_resize(GFX_Context *context, GFX_Swapchain *swapchain, uint32_t new_width, uint32_t new_height);
bool gfx_image_resize(GFX_Context *context, GFX_Image *image, uint32_t new_width, uint32_t new_height);

bool gfx_bind(GFX_Context *context, GFX_Pipeline *pipeline, uint32_t set, Uniform *uniforms, uint32_t uniform_count);

// pushes data to command context staging ring buffer, aligned to 256
uint64_t gfx_cmd_put(GFX_CommandContext *cmd, uint64_t size, void *src);
void gfx_cmd_buffer_to_buffer(GFX_CommandContext *cmd, GFX_Buffer *dst, GFX_Buffer *src, uint64_t dst_offset, uint64_t src_offset, uint64_t size);
void gfx_cmd_buffer_to_image(GFX_CommandContext *cmd, GFX_Image *dst, GFX_Buffer *src, uint64_t src_offset, uint32_t width, uint32_t height);
void gfx_cmd_buffer_barrier(GFX_CommandContext *cmd, ResourceUsage src, ResourceUsage dst, uint64_t offset, uint64_t size, GFX_Buffer *target);
bool gfx_cmd_image_barrier(GFX_CommandContext *cmd, ResourceUsage src, ResourceUsage dst, uint32_t base_miplevel, uint32_t level_count, GFX_Image *target);
bool gfx_cmd_image_transition(GFX_CommandContext *cmd, ResourceUsage dst, GFX_Image *target);
void gfx_cmd_image_blit(GFX_CommandContext *cmd, Rectangle source_rect, GFX_Image *source, Rectangle target_rect, GFX_Image *target);
void gfx_cmd_image_upload(GFX_CommandContext *cmd, GFX_Image *image, uint32_t width, uint32_t height, void *pixels);
void gfx_cmd_buffer_upload(GFX_CommandContext *cmd, GFX_Buffer *buffer, uint64_t offset, uint64_t size, void *data);

void gfx_cmd_pipeline_bind(GFX_CommandContext *cmd, GFX_Pipeline *pipeline);
void gfx_cmd_dispatch(GFX_CommandContext *context, uint32_t x, uint32_t y, uint32_t z);

static inline uint32_t gfx_image_id(GFX_Context *context, GFX_Image *image) {
	if (gfx_image_valid(context, image) == false)
		return 0;

	return indexof(context->image_pool, image);
}
