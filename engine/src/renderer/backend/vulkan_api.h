#pragma once

#include "renderer/r_internal.h"

#include "common.h"

struct window;

typedef struct vulkan_context VulkanContext;

#define MAX_BUFFERS 1024
#define MAX_TEXTURES 512
#define MAX_SAMPLERS 32
#define MAX_SHADERS 32
#define MAX_UNIFORM_SETS 1024

bool vulkan_renderer_make(Arena *arena, struct window *display, VulkanContext **out_context);
void vulkan_renderer_destroy(VulkanContext *context);
bool vulkan_on_resize(VulkanContext *context, uint32_t new_width, uint32_t new_height);

bool vulkan_renderer_frame_begin(VulkanContext *context, uint32_t width, uint32_t height);
bool Vulkan_renderer_frame_end(VulkanContext *context);

bool vulkan_renderer_drawlist_begin(VulkanContext *context, DrawListDesc desc);
bool vulkan_renderer_drawlist_end(VulkanContext *context);

// bool vulkan_renderer_compute_list_begin(VulkanContext *context, ...);
// bool vulkan_renderer_compute_list_end(VulkanContext *context);

ENGINE_API bool vulkan_renderer_draw(VulkanContext *context, uint32_t vertex_count);
ENGINE_API bool vulkan_renderer_draw_indexed(VulkanContext *context, uint32_t index_count);

ENGINE_API RhiShader vulkan_shader_make(
	Arena *arena, VulkanContext *context,
	ShaderConfig *config, ShaderReflection *out_reflection);
bool vulkan_shader_destroy(VulkanContext *context, RhiShader shader);

ENGINE_API bool vulkan_shader_bind(
	VulkanContext *context, RhiShader rshader, PipelineDesc desc);

ENGINE_API RhiTexture vulkan_texture_make(VulkanContext *context, uint32_t width, uint32_t height, TextureType type, TextureFormat format, TextureUsageFlags usage, uint8_t *pixels);
bool vulkan_texture_destroy(VulkanContext *context, RhiTexture texture);
bool vulkan_texture_prepare_attachment(VulkanContext *context, RhiTexture texture);
bool vulkan_texture_prepare_sample(VulkanContext *context, RhiTexture texture);
bool vulkan_texture_resize(VulkanContext *context, RhiTexture texture, uint32_t width, uint32_t height);

ENGINE_API RhiBuffer vulkan_buffer_make(VulkanContext *context, BufferType type, size_t size, void *data);
ENGINE_API bool vulkan_buffer_destroy(VulkanContext *context, RhiBuffer buffer);
ENGINE_API bool vulkan_buffer_write(VulkanContext *context, RhiBuffer buffer, size_t offset, size_t size, void *data);
ENGINE_API bool vulkan_buffer_bind(VulkanContext *context, RhiBuffer buffer, size_t index_size);
ENGINE_API bool vulkan_buffers_bind(VulkanContext *context, RhiBuffer *buffers, uint32_t count);

RhiSampler vulkan_sampler_make(VulkanContext *context, SamplerDesc description);
bool vulkan_sampler_destroy(VulkanContext *context, RhiSampler sampler);

ENGINE_API RhiUniformSet vulkan_uniformset_make(VulkanContext *context, RhiShader shader, uint32_t set_number);
ENGINE_API RhiUniformSet vulkan_uniformset_make_ex(VulkanContext *context, ResourceBinding *bindings, uint32_t binding_count);
ENGINE_API bool vulkan_uniformset_destroy(VulkanContext *context, RhiUniformSet set);

ENGINE_API bool vulkan_uniformset_bind_buffer(VulkanContext *context, RhiUniformSet set, uint32_t binding, RhiBuffer buffer);
ENGINE_API bool vulkan_uniformset_bind_texture(VulkanContext *context, RhiUniformSet set, uint32_t binding, RhiTexture texture, RhiSampler sampler);
ENGINE_API bool vulkan_uniformset_bind(VulkanContext *context, RhiUniformSet uniform);

ENGINE_API bool vulkan_renderer_push_constants(VulkanContext *context, size_t offset, size_t size, void *data);
