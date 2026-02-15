#pragma once

#include "renderer/r_internal.h"

#include "common.h"

struct platform;

typedef struct vulkan_context VulkanContext;

#define MAX_BUFFERS 1024
#define MAX_TEXTURES 512
#define MAX_SAMPLERS 32
#define MAX_SHADERS 32
#define MAX_UNIFORM_SETS 1024

bool vulkan_renderer_create(Arena *arena, struct platform *platform, VulkanContext **out_context);
void vulkan_renderer_destroy(VulkanContext *context);
bool vulkan_renderer_on_resize(VulkanContext *context, uint32_t new_width, uint32_t new_height);

bool vulkan_renderer_frame_begin(VulkanContext *context, uint32_t width, uint32_t height);
bool Vulkan_renderer_frame_end(VulkanContext *context);

bool vulkan_renderer_draw_list_begin(VulkanContext *context, DrawListDesc desc);
bool vulkan_renderer_draw_list_end(VulkanContext *context);

// bool vulkan_renderer_compute_list_begin(VulkanContext *context, ...);
// bool vulkan_renderer_compute_list_end(VulkanContext *context);

ENGINE_API bool vulkan_renderer_draw(VulkanContext *context, uint32_t vertex_count);
bool vulkan_renderer_draw_indexed(VulkanContext *context, uint32_t index_count);

ENGINE_API RhiShader vulkan_renderer_shader_create(
	Arena *arena, VulkanContext *context,
	ShaderConfig *config, ShaderReflection *out_reflection);
bool vulkan_renderer_shader_destroy(VulkanContext *context, RhiShader shader);

ENGINE_API bool vulkan_renderer_shader_bind(
	VulkanContext *context, RhiShader rshader, ShaderStateFlags flags);

ENGINE_API RhiTexture vulkan_renderer_texture_create(VulkanContext *context, uint32_t width, uint32_t height, TextureType type, TextureFormat format, TextureUsageFlags usage, uint8_t *pixels);
bool vulkan_renderer_texture_destroy(VulkanContext *context, RhiTexture texture);
bool vulkan_renderer_texture_prepare_attachment(VulkanContext *context, RhiTexture texture);
bool vulkan_renderer_texture_prepare_sample(VulkanContext *context, RhiTexture texture);
bool vulkan_renderer_texture_resize(VulkanContext *context, RhiTexture texture, uint32_t width, uint32_t height);

ENGINE_API RhiBuffer vulkan_renderer_buffer_create(VulkanContext *context, BufferType type, size_t size, void *data);
ENGINE_API bool vulkan_renderer_buffer_destroy(VulkanContext *context, RhiBuffer buffer);
ENGINE_API bool vulkan_renderer_buffer_write(VulkanContext *context, RhiBuffer buffer, size_t offset, size_t size, void *data);
ENGINE_API bool vulkan_renderer_buffer_bind(VulkanContext *context, RhiBuffer buffer, size_t index_size);
ENGINE_API bool vulkan_renderer_buffers_bind(VulkanContext *context, RhiBuffer *buffers, uint32_t count);

RhiSampler vulkan_renderer_sampler_create(VulkanContext *context, SamplerDesc description);
bool vulkan_renderer_sampler_destroy(VulkanContext *context, RhiSampler sampler);

ENGINE_API RhiUniformSet vulkan_renderer_uniform_set_create(VulkanContext *context, RhiShader shader, uint32_t set_number);
ENGINE_API RhiUniformSet vulkan_renderer_uniform_set_create_ex(VulkanContext *context, ResourceBinding *bindings, uint32_t binding_count);
ENGINE_API bool vulkan_renderer_uniform_set_destroy(VulkanContext *context, RhiUniformSet set);

ENGINE_API bool vulkan_renderer_uniform_set_bind_buffer(VulkanContext *context, RhiUniformSet set, uint32_t binding, RhiBuffer buffer);
ENGINE_API bool vulkan_renderer_uniform_set_bind_texture(VulkanContext *context, RhiUniformSet set, uint32_t binding, RhiTexture texture, RhiSampler sampler);
ENGINE_API bool vulkan_renderer_uniform_set_bind(VulkanContext *context, RhiUniformSet uniform);

ENGINE_API bool vulkan_renderer_push_constants(VulkanContext *context, size_t offset, size_t size, void *data);
