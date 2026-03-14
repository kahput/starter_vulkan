#pragma once

#include "renderer/r_internal.h"

#include "common.h"

struct window;

typedef struct vulkan_context VulkanContext;

#define MAX_BUFFERS 1024
#define MAX_TEXTURES 512
#define MAX_SAMPLERS 32
#define MAX_SHADERS 32
#define MAX_UNIFORM_SETS 4096

bool vulkan_renderer_make(Arena *arena, struct window *display, VulkanContext **out_context);
void vulkan_renderer_destroy(VulkanContext *context);
bool vulkan_renderer_on_resize(VulkanContext *context, uint32_t new_width, uint32_t new_height);

ENGINE_API bool vulkan_frame_begin(VulkanContext *context, uint32_t width, uint32_t height);
ENGINE_API bool vulkan_frame_end(VulkanContext *context);

ENGINE_API bool vulkan_drawlist_begin(VulkanContext *context, DrawListDesc desc);
ENGINE_API bool vulkan_drawlist_end(VulkanContext *context);

// bool vulkan_renderer_compute_list_begin(VulkanContext *context, ...);
// bool vulkan_renderer_compute_list_end(VulkanContext *context);

ENGINE_API bool vulkan_renderer_draw(VulkanContext *context, uint32_t vertex_count);
ENGINE_API bool vulkan_renderer_draw_indexed(VulkanContext *context, uint32_t index_count);
ENGINE_API bool vulkan_renderer_draw_offset(VulkanContext *context, uint32_t vertex_count, uint32_t start_vertex);

ENGINE_API RhiShader vulkan_shader_make(
	Arena *arena, VulkanContext *context,
	ShaderConfig config, ShaderReflection *out_reflection);
bool vulkan_shader_destroy(VulkanContext *context, RhiShader shader);

ENGINE_API bool vulkan_shader_bind(
	VulkanContext *context, RhiShader rshader, PipelineDesc desc);

ENGINE_API RhiTexture vulkan_texture_make(VulkanContext *context, uint32_t width, uint32_t height, TextureType type, TextureFormat format, TextureUsageFlags usage, uint8_t *pixels);
ENGINE_API bool vulkan_texture_destroy(VulkanContext *context, RhiTexture texture);
ENGINE_API bool vulkan_texture_prepare_attachment(VulkanContext *context, RhiTexture texture);
ENGINE_API bool vulkan_texture_prepare_sample(VulkanContext *context, RhiTexture texture);
ENGINE_API bool vulkan_texture_resize(VulkanContext *context, RhiTexture texture, uint32_t width, uint32_t height);
ENGINE_API uint32_2 vulkan_texture_size(VulkanContext *context, RhiTexture texture);

ENGINE_API RhiBuffer vulkan_buffer_make(VulkanContext *context, BufferType type, BufferMemory memory, size_t size, void *data);
ENGINE_API RhiBuffer vulkan_bufferarray_make(VulkanContext *context, BufferType type, BufferMemory memory, uint32_t count, size_t stride);
ENGINE_API bool vulkan_buffer_destroy(VulkanContext *context, RhiBuffer buffer);
ENGINE_API bool vulkan_buffer_write(VulkanContext *context, RhiBuffer buffer, size_t offset, size_t size, void *data);
ENGINE_API bool vulkan_buffer_bind(VulkanContext *context, RhiBuffer rbuffer, size_t offset);
ENGINE_API bool vulkan_buffers_bind(VulkanContext *context, RhiBuffer *buffers, uint32_t count);

ENGINE_API RhiSampler vulkan_sampler_make(VulkanContext *context, SamplerDesc description);
ENGINE_API bool vulkan_sampler_destroy(VulkanContext *context, RhiSampler sampler);

ENGINE_API RhiUniformSet vulkan_uniformset_push(VulkanContext *context, RhiShader shader, uint32_t set_number); // Transient
ENGINE_API bool vulkan_uniformset_bind_buffer(VulkanContext *context, RhiUniformSet set, uint32_t binding, RhiBuffer buffer);
ENGINE_API bool vulkan_uniformset_bind_texture(VulkanContext *context, RhiUniformSet set, uint32_t binding, RhiTexture texture, RhiSampler sampler);
ENGINE_API bool vulkan_uniformset_bind(VulkanContext *context, RhiUniformSet uniform);

ENGINE_API bool vulkan_push_constants(VulkanContext *context, size_t offset, size_t size, void *data);
