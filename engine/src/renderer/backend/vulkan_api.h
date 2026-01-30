#pragma once

#include "renderer/r_internal.h"

#include "common.h"

struct platform;

typedef struct vulkan_context VulkanContext;

#define MAX_BUFFERS 1024
#define MAX_TEXTURES 512
#define MAX_SAMPLERS 32
#define MAX_RENDER_TARGETS 32

#define MAX_SHADERS 32
#define MAX_SHADER_VARIANTS 8
#define MAX_GLOBAL_RESOURCES 32
#define MAX_GROUP_RESOURCES 256
#define MAX_GEOMETRY_RESOURCES 1024
#define MAX_RENDER_PASSES 8

bool vulkan_renderer_create(Arena *arena, struct platform *platform, VulkanContext **out_context);
void vulkan_renderer_destroy(VulkanContext *context);
bool vulkan_renderer_on_resize(VulkanContext *context, uint32_t new_width, uint32_t new_height);

bool vulkan_renderer_frame_begin(VulkanContext *context, uint32_t width, uint32_t height);
bool Vulkan_renderer_frame_end(VulkanContext *context);

RhiPass vulkan_renderer_pass_create(VulkanContext *context, RenderPassDesc desc);
bool vulkan_renderer_pass_destroy(VulkanContext *context, RhiPass pass);
bool vulkan_renderer_pass_begin(VulkanContext *context, RhiPass pass);
bool vulkan_renderer_pass_end(VulkanContext *context);

ENGINE_API bool vulkan_renderer_draw(VulkanContext *context, uint32_t vertex_count);
bool vulkan_renderer_draw_indexed(VulkanContext *context, uint32_t index_count);

ENGINE_API RhiShader vulkan_renderer_shader_create(Arena *arena, VulkanContext *context, RhiGlobalResource rglobal, ShaderConfig *config, ShaderReflection *out_reflection);
bool vulkan_renderer_shader_destroy(VulkanContext *context, RhiShader shader);
ENGINE_API bool vulkan_renderer_shader_bind(VulkanContext *context, RhiShader rshader, uint32_t variant_index);

ENGINE_API RhiShaderVariant vulkan_renderer_shader_variant_create(VulkanContext *context, RhiShader shader, RhiPass compatible_pass, PipelineDesc description);
bool vulkan_renderer_shader_variant_destroy(VulkanContext *context, RhiShader shader, RhiShaderVariant variant);

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

RhiGlobalResource vulkan_renderer_resource_global_create(VulkanContext *context, ResourceBinding *bindings, uint32_t binding_count);
bool vulkan_renderer_resource_global_destroy(VulkanContext *context, RhiGlobalResource resource);
bool vulkan_renderer_resource_global_write(VulkanContext *context, RhiGlobalResource resource, size_t offset, size_t size, void *data);
bool vulkan_renderer_resource_global_bind(VulkanContext *context, RhiGlobalResource resource);
bool vulkan_renderer_resource_global_set_texture_sampler(VulkanContext *context, RhiGlobalResource resource, uint32_t binding, RhiTexture texture, RhiSampler sampler);

ENGINE_API RhiGroupResource vulkan_renderer_resource_group_create(VulkanContext *context, RhiShader shader, uint32_t max_instance_count);
bool vulkan_renderer_resource_group_destroy(VulkanContext *context, RhiGroupResource resource);
ENGINE_API bool vulkan_renderer_resource_group_write(VulkanContext *context, RhiGroupResource resource, uint32_t instance_index, size_t offset, size_t size, void *data, bool all_frames);
ENGINE_API bool vulkan_renderer_resource_group_bind(VulkanContext *context, RhiGroupResource resource, uint32_t instance_index);
ENGINE_API bool vulkan_renderer_resource_group_set_texture_sampler(VulkanContext *context, RhiGroupResource resource, uint32_t binding, RhiTexture texture, RhiSampler sampler);

ENGINE_API bool vulkan_renderer_resource_local_write(VulkanContext *context, size_t offset, size_t size, void *data);
