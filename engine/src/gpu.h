#pragma once

#include "common.h"
#include "core/arena.h"

#include "gpu/gpu_types.h"

bool gpu_device_make(GPU_Device *device);
void gpu_device_destroy(GPU_Device *device);
void gpu_device_wait_idle(GPU_Device *device);

/* bool gpu_wait(GPU_Device *device, GPU_Fence fence); */
// bool gpu_fence_done(GPU_Device *device, GPU_Fence *fence);

GPU_CommandList *gpu_transfer_cmd(GPU_Device *device);
bool gpu_transfer_flush(GPU_Device *device);

GPU_Buffer *gpu_buffer_make(GPU_Device *device, uint64_t size, BufferOptions options);
GPU_Image *gpu_image_make(GPU_Device *device, uint32_t width, uint32_t height, ImageOptions options);
// GPU_ImageView *gpu_image_view(GPU_Image *img, ViewOptions opt);
GPU_Sampler *gpu_sampler_make(GPU_Device *device, SamplerOptions opt);
GPU_Swapchain *gpu_swapchain_make(GPU_Device *device, WM_Surface *surface, const char *debug_name);

bool gpu_reflect_shader_uniforms(string8 bytecode, UniformSet out_sets[GPU_LIMIT_UNIFORM_SETS]);
GPU_Shader *gpu_compute_make(GPU_Device *device, string8 bytecode, const char *debug_name);
GPU_Shader *gpu_shader_make(GPU_Device *device, string8 vs_bytecode, string8 fs_bytecode, const char *debug_name);

int32_t gpu_pipeline_register(GPU_Device *device, GPU_Shader *shader, PipelineOptions options);
GPU_Pipeline *gpu_pipeline_ensure(GPU_Device *device, GPU_Shader *shader, PipelineOptions options, GPU_DrawTargetLayout layout);

bool gpu_destroy(GPU_Device *device, RID id);

INLINE bool gpu_buffer_destroy(GPU_Device *device, GPU_Buffer *buffer) { return buffer ? gpu_destroy(device, buffer->id) : false; }
INLINE bool gpu_image_destroy(GPU_Device *device, GPU_Image *image) { return image ? gpu_destroy(device, image->id) : false; }
INLINE bool gpu_sampler_destroy(GPU_Device *device, GPU_Sampler *sampler) { return sampler ? gpu_destroy(device, sampler->id) : false; }
INLINE bool gpu_swapchain_destroy(GPU_Device *device, GPU_Swapchain *swapchain) { return swapchain ? gpu_destroy(device, swapchain->id) : false; }
bool gpu_shader_destroy(GPU_Device *device, GPU_Shader *shader);
bool gpu_pipeline_destroy(GPU_Device *device, GPU_Pipeline *pipeline);

GPU_Image *gpu_swapchain_backbuffer(GPU_Device *device, GPU_CommandList *cmd, GPU_Swapchain *swapchain);
// void gpu_present(GPU_Device *device, GPU_Swapchain *swapchain, GPU_Image *image);
bool gpu_swapchain_resize(GPU_Device *device, GPU_Swapchain *swapchain, uint32_t new_width, uint32_t new_height);
bool gpu_image_resize(GPU_Device *device, GPU_Image *image, uint32_t new_width, uint32_t new_height);

GPU_CommandList *gpu_cmd_begin(GPU_Device *device); // TODO: GPU_DeviceQueue for selection
void gpu_cmd_end(GPU_Device *device, GPU_CommandList *cmd);

GPU_CommandList *gpu_frame_begin(GPU_Device *device);
bool gpu_frame_end(GPU_Device *device, GPU_CommandList *cmd);
void gpu_cmd_label(GPU_CommandList *cmd, const char *name);

void gpu_pass_begin(GPU_CommandList *cmd, GPU_DrawPassInfo info);
void gpu_pass_end(GPU_CommandList *cmd);
GPU_DrawTargetLayout gpu_cmd_active_layout(GPU_CommandList *cmd);

// pushes data to command context staging ring buffer, aligned to 256
uint64_t gpu_cmd_put(GPU_CommandList *cmd, uint64_t size, void *src);
void gpu_cmd_buffer_to_buffer(GPU_CommandList *cmd, GPU_Buffer *dst, GPU_Buffer *src, uint64_t dst_offset, uint64_t src_offset, uint64_t size);
void gpu_cmd_buffer_to_image(GPU_CommandList *cmd, GPU_Image *dst, GPU_Buffer *src, uint64_t src_offset, uint32_t width, uint32_t height);
void gpu_cmd_buffer_barrier(GPU_CommandList *cmd, ResourceUsage src, ResourceUsage dst, uint64_t offset, uint64_t size, GPU_Buffer *target);
bool gpu_cmd_image_barrier(GPU_CommandList *cmd, ResourceUsage src, ResourceUsage dst, uint32_t base_miplevel, uint32_t level_count, GPU_Image *target);
bool gpu_cmd_image_transition(GPU_CommandList *cmd, ResourceUsage dst, GPU_Image *target);
void gpu_cmd_image_blit(GPU_CommandList *cmd, Rectangle source_rect, GPU_Image *source, Rectangle target_rect, GPU_Image *target);
void gpu_cmd_image_clear(GPU_CommandList *cmd, Rectangle rect, rgba8 color, GPU_Image *image);
void gpu_cmd_image_upload(GPU_CommandList *cmd, GPU_Image *image, uint32_t width, uint32_t height, void *pixels);
void gpu_cmd_buffer_upload(GPU_CommandList *cmd, GPU_Buffer *buffer, uint64_t offset, uint64_t size, void *data);

void gpu_cmd_viewport(GPU_CommandList *cmd, Rectangle area);
void gpu_cmd_scissor(GPU_CommandList *cmd, Rectangle area);

void gpu_cmd_shader_bind(GPU_Device *device, GPU_Shader *shader, uint32_t pipeline_index);
void gpu_cmd_pipeline_bind(GPU_CommandList *cmd, GPU_Pipeline *pipeline);
bool gpu_cmd_bind(GPU_Device *device, uint32_t set_index, Uniform *uniforms, uint32_t uniform_count);

void gpu_cmd_bind_index_buffer16(GPU_CommandList *cmd, GPU_Buffer *buffer, uint64_t offset);
void gpu_cmd_bind_index_buffer32(GPU_CommandList *cmd, GPU_Buffer *buffer, uint64_t offset);
void gpu_cmd_push_constant(GPU_CommandList *cmd, uint64_t size, void *data);
void gpu_cmd_dispatch(GPU_CommandList *cmd, uint32_t x, uint32_t y, uint32_t z);

void gpu_cmd_draw(GPU_CommandList *cmd, uint32_t vertex_offset, uint32_t vertex_count);
void gpu_cmd_draw_instanced(GPU_CommandList *cmd, uint32_t vertex_offset, uint32_t vertex_count, uint32_t instance_offset, uint32_t instance_count);

void gpu_cmd_draw_indexed(GPU_CommandList *cmd, uint32_t index_offset, uint32_t index_count, uint32_t vertex_offset);
void gpu_cmd_draw_indexed_instanced(GPU_CommandList *cmd, uint32_t index_offset, uint32_t index_count, uint32_t vertex_offset, uint32_t instance_offset, uint32_t instance_count);
