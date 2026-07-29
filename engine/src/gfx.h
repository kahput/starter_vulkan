#pragma once

#include "common.h"
#include "core/arena.h"

#include "gfx/gfx_types.h"

static inline bool gfx_device_valid(GFX_Device *device) {
	return device && device->handle;
}

static inline bool gfx_buffer_valid(GFX_Device *device, GFX_Buffer *buffer) {
	return gfx_device_valid(device) && buffer && buffer > device->buffer_pool && buffer < device->buffer_pool + MAX_BUFFERS;
}

static inline bool gfx_image_valid(GFX_Device *device, GFX_Image *image) {
	return gfx_device_valid(device) && image && image > device->image_pool && image < device->image_pool + MAX_IMAGES;
}

static inline bool gfx_sampler_valid(GFX_Device *device, GFX_Sampler *sampler) {
	return gfx_device_valid(device) && sampler && sampler > device->sampler_pool && sampler < device->sampler_pool + MAX_SAMPLERS;
}

static inline bool gfx_shader_valid(GFX_Device *device, GFX_Shader *shader) {
	return gfx_device_valid(device) && shader && shader > device->shader_pool && shader < device->shader_pool + MAX_SHADERS;
}

static inline bool gfx_pipeline_valid(GFX_Device *device, GFX_Pipeline *pipeline) {
	return gfx_device_valid(device) && pipeline && pipeline > device->pipeline_pool && pipeline < device->pipeline_pool + MAX_PIPELINES;
}

static inline bool gfx_swapchain_valid(GFX_Device *device, GFX_Swapchain *swapchain) {
	return gfx_device_valid(device) && swapchain && swapchain > device->swapchain_pool && swapchain < device->swapchain_pool + MAX_SWAPCHAINS;
}

bool gfx_device_make(GFX_Device *device);
void gfx_device_destroy(GFX_Device *device);

GFX_Command *gfx_frame_begin(GFX_Device *device);
bool gfx_frame_end(GFX_Device *device, GFX_Command *cmd);

GFX_Command *gfx_transfer_cmd(GFX_Device *device);
bool gfx_transfer_flush(GFX_Device *device);

GFX_Buffer *gfx_buffer_make(GFX_Device *device, uint64_t size, BufferOptions options);
GFX_Image *gfx_image_make(GFX_Device *device, uint32_t width, uint32_t height, ImageOptions options);
GFX_Sampler *gfx_sampler_make(GFX_Device *device, SamplerOptions opt);
GFX_Swapchain *gfx_swapchain_make(GFX_Device *device, OS_Surface *surface, const char *debug_name);

bool gfx_reflect_shader_uniforms(String8 bytecode, UniformSet out_sets[GFX_LIMIT_UNIFORM_SETS]);
GFX_Shader *gfx_compute_make(GFX_Device *device, String8 bytecode, const char *debug_name);
GFX_Shader *gfx_shader_make(GFX_Device *device, String8 vs_bytecode, String8 fs_bytecode, const char *debug_name);
GFX_Pipeline *gfx_pipeline_ensure(GFX_Device *device, GFX_Shader *shader, PipelineOptions options);

bool gfx_buffer_destroy(GFX_Device *device, GFX_Buffer *buffer);
bool gfx_image_destroy(GFX_Device *device, GFX_Image *image);
bool gfx_sampler_destroy(GFX_Device *device, GFX_Sampler *sampler);
bool gfx_swapchain_destroy(GFX_Device *device, GFX_Swapchain *surface);
bool gfx_shader_destroy(GFX_Device *device, GFX_Shader *shader);
bool gfx_pipeline_destroy(GFX_Device *device, GFX_Pipeline *pipeline);

bool gfx_buffer_queue_destroy(GFX_Device *device, GFX_Buffer *buffer);
bool gfx_image_queue_destroy(GFX_Device *device, GFX_Image *image);
bool gfx_sampler_queue_destroy(GFX_Device *device, GFX_Sampler *sampler);

GFX_Image *gfx_backbuffer(GFX_Device *device, GFX_Command *cmd, GFX_Swapchain *swapchain);
void gfx_present(GFX_Device *device, GFX_Swapchain *swapchain, GFX_Image *image);
bool gfx_swapchain_resize(GFX_Device *device, GFX_Swapchain *swapchain, uint32_t new_width, uint32_t new_height);
bool gfx_image_resize(GFX_Device *device, GFX_Image *image, uint32_t new_width, uint32_t new_height);

bool gfx_cmd_bind(GFX_Device *device, uint32_t set_index, Uniform *uniforms, uint32_t uniform_count);

// pushes data to command context staging ring buffer, aligned to 256
uint64_t gfx_cmd_put(GFX_Command *cmd, uint64_t size, void *src);
void gfx_cmd_buffer_to_buffer(GFX_Command *cmd, GFX_Buffer *dst, GFX_Buffer *src, uint64_t dst_offset, uint64_t src_offset, uint64_t size);
void gfx_cmd_buffer_to_image(GFX_Command *cmd, GFX_Image *dst, GFX_Buffer *src, uint64_t src_offset, uint32_t width, uint32_t height);
void gfx_cmd_buffer_barrier(GFX_Command *cmd, ResourceUsage src, ResourceUsage dst, uint64_t offset, uint64_t size, GFX_Buffer *target);
bool gfx_cmd_image_barrier(GFX_Command *cmd, ResourceUsage src, ResourceUsage dst, uint32_t base_miplevel, uint32_t level_count, GFX_Image *target);
bool gfx_cmd_image_transition(GFX_Command *cmd, ResourceUsage dst, GFX_Image *target);
void gfx_cmd_image_blit(GFX_Command *cmd, Rectangle source_rect, GFX_Image *source, Rectangle target_rect, GFX_Image *target);
void gfx_cmd_image_clear(GFX_Command *cmd, Rectangle rect, Color color, GFX_Image *image);
void gfx_cmd_image_upload(GFX_Command *cmd, GFX_Image *image, uint32_t width, uint32_t height, void *pixels);
void gfx_cmd_buffer_upload(GFX_Command *cmd, GFX_Buffer *buffer, uint64_t offset, uint64_t size, void *data);

void gfx_cmd_viewport(GFX_Command *cmd, Rectangle area);
void gfx_cmd_scissor(GFX_Command *cmd, Rectangle area);

void gfx_cmd_shader_bind(GFX_Command *cmd, GFX_Shader *shader); // binds first pipeline
void gfx_cmd_pipeline_bind(GFX_Command *cmd, GFX_Pipeline *pipeline);

void gfx_cmd_dispatch(GFX_Command *cmd, uint32_t x, uint32_t y, uint32_t z);

static inline uint32_t gfx_image_id(GFX_Device *device, GFX_Image *image) {
	if (gfx_image_valid(device, image) == false)
		return 0;

	return indexof(device->image_pool, image);
}
