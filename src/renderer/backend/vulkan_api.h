#pragma once

#include "common.h"

// TODO: Maybe move need structs to core_renderer_types.h
#include "renderer.h"

#include "renderer/r_internal.h"

#include "core/astring.h"

struct platform;

typedef struct vulkan_context VulkanContext;

#define MAX_BUFFERS 2048
#define MAX_TEXTURES 512
#define MAX_SAMPLERS 32
#define MAX_SHADERS 32
#define MAX_PIPELINES 32
#define MAX_RESOURCE_SETS 128

bool vulkan_renderer_create(Arena *arena, struct platform *platform, VulkanContext **out_context);
void vulkan_renderer_destroy(VulkanContext *context);
bool vulkan_renderer_on_resize(VulkanContext *context, uint32_t new_width, uint32_t new_height);

bool vulkan_renderer_frame_begin(VulkanContext *context, struct platform *platform);
bool Vulkan_renderer_frame_end(VulkanContext *context);

bool vulkan_renderer_draw(VulkanContext *context, uint32_t vertex_count);
bool vulkan_renderer_draw_indexed(VulkanContext *context, uint32_t index_count);

bool vulkan_renderer_shader_create(Arena *arena, VulkanContext *context, uint32_t store_index, ShaderConfig *config, PipelineDesc description, ShaderReflection *out_reflection);
bool vulkan_renderer_shader_destroy(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_shader_bind(VulkanContext *context, uint32_t shader_index, uint32_t resource_index);
// TODO: Variants
// bool vulkan_renderer_shader_variant_create(VulkanContext *context, uint32_t shader_index, uint32_t variant_index, PipelineDesc description);
// bool vulkan_renderer_shader_variant_destroy(VulkanContext *context, uint32_t shader_index, uint32_t variant_index, PipelineDesc description);
// bool vulkan_renderer_shader_variant_set(VulkanContext *context, uint32_t shader_index, uint32_t variant_index);
// bool vulkan_renderer_shader_variant_lock(VulkanContext *context, uint32_t shader_index, uint32_t variant_index);

bool vulkan_renderer_shader_resource_create(VulkanContext *context, uint32_t store_index, uint32_t shader_index);
bool vulkan_renderer_global_resource_set_buffer(VulkanContext *context, uint32_t buffer_index);
bool vulkan_renderer_shader_resource_set_buffer(VulkanContext *context, uint32_t shader_index, uint32_t resource_index, uint32_t binding, uint32_t buffer_index);
bool vulkan_renderer_shader_resource_set_texture_sampler(VulkanContext *context, uint32_t shader_index, uint32_t resource_index, uint32_t binding, uint32_t texture_index, uint32_t sampler_index);

void vulkan_renderer_shader_global_state_wireframe_set(VulkanContext *context, bool active);

bool vulkan_renderer_texture_create(VulkanContext *context, uint32_t store_index, uint32_t width, uint32_t height, uint32_t channels, bool is_srgb, uint8_t *pixels);
bool vulkan_renderer_texture_destroy(VulkanContext *context, uint32_t retrieve_index);

bool vulkan_renderer_buffer_create(VulkanContext *context, uint32_t store_index, BufferType type, size_t size, void *data);
bool vulkan_renderer_buffer_destroy(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_buffer_write(VulkanContext *context, uint32_t retrieve_index, size_t offset, size_t size, void *data);
bool vulkan_renderer_buffer_bind(VulkanContext *context, uint32_t retrieve_index, size_t index_size);
bool vulkan_renderer_buffers_bind(VulkanContext *context, uint32_t *buffers, uint32_t count);

bool vulkan_renderer_sampler_create(VulkanContext *context, uint32_t store_index, SamplerDesc description);
bool vulkan_renderer_sampler_destroy(VulkanContext *context, uint32_t retrieve_index);

bool vulkan_renderer_push_constants(VulkanContext *context, uint32_t shader_index, size_t offset, size_t size, void *data);
