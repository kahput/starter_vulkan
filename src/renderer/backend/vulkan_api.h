#pragma once

#include "renderer/r_internal.h"

#include "common.h"
#include "core/astring.h"

struct platform;

typedef struct vulkan_context VulkanContext;

#define MAX_BUFFERS 1024
#define MAX_TEXTURES 512
#define MAX_SAMPLERS 32
#define MAX_RENDER_TARGETS 32

#define MAX_SHADERS 32
#define MAX_GLOBAL_RESOURCES 32
#define MAX_GROUP_RESOURCES 256
#define MAX_GEOMETRY_RESOURCES 1024
#define MAX_RENDER_PASSES 8

bool vulkan_renderer_create(Arena *arena, struct platform *platform, VulkanContext **out_context);
void vulkan_renderer_destroy(VulkanContext *context);
bool vulkan_renderer_on_resize(VulkanContext *context, uint32_t new_width, uint32_t new_height);

bool vulkan_renderer_frame_begin(VulkanContext *context, uint32_t width, uint32_t height);
bool Vulkan_renderer_frame_end(VulkanContext *context);

// TODO: Render passes
bool vulkan_renderer_pass_create(VulkanContext *context, uint32_t store_index, uint32_t global_resource, RenderPassDesc *desc);
bool vulkan_renderer_pass_destroy(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_pass_begin(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_pass_end(VulkanContext *context);

bool vulkan_renderer_draw(VulkanContext *context, uint32_t vertex_count);
bool vulkan_renderer_draw_indexed(VulkanContext *context, uint32_t index_count);

bool vulkan_renderer_shader_create(
	Arena *arena,
	VulkanContext *context,
	uint32_t store_index, uint32_t compatible_pass,
	ShaderConfig *config, PipelineDesc description, ShaderReflection *out_reflection);
bool vulkan_renderer_shader_destroy(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_shader_bind(VulkanContext *context, uint32_t shader_index);
// TODO: Variants
// bool vulkan_renderer_shader_variant_create(VulkanContext *context, uint32_t shader_index, uint32_t variant_index, PipelineDesc description);
// bool vulkan_renderer_shader_variant_destroy(VulkanContext *context, uint32_t shader_index, uint32_t variant_index, PipelineDesc description);
// bool vulkan_renderer_shader_variant_set(VulkanContext *context, uint32_t shader_index, uint32_t variant_index);
// bool vulkan_renderer_shader_variant_lock(VulkanContext *context, uint32_t shader_index, uint32_t variant_index);

void vulkan_renderer_shader_global_state_wireframe_set(VulkanContext *context, bool active);

#define MATCH_SWAPCHAIN 0
bool vulkan_renderer_texture_create(VulkanContext *context, uint32_t store_index, uint32_t width, uint32_t height, uint32_t channels, bool is_srgb, TextureUsageFlags usage, uint8_t *pixels);
bool vulkan_renderer_texture_resize(VulkanContext *context, uint32_t retrieve_index, uint32_t width, uint32_t height);
bool vulkan_renderer_texture_destroy(VulkanContext *context, uint32_t retrieve_index);

// TODO: Geometry as first class resources
// bool vulkan_renderer_geometry_create(VulkanContext *context, uint32_t store_index, void *vertices, size_t vertex_size, void *indices, size_t i_size);
// bool vulkan_renderer_geometry_destroy(VulkanContext *context, uint32_t retrieve_index);
// bool vulkan_renderer_geometry_draw(VulkanContext *context, uint32_t retrieve_index);

bool vulkan_renderer_buffer_create(VulkanContext *context, uint32_t store_index, BufferType type, size_t size, void *data);
bool vulkan_renderer_buffer_destroy(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_buffer_write(VulkanContext *context, uint32_t retrieve_index, size_t offset, size_t size, void *data);
bool vulkan_renderer_buffer_bind(VulkanContext *context, uint32_t retrieve_index, size_t index_size);
bool vulkan_renderer_buffers_bind(VulkanContext *context, uint32_t *buffers, uint32_t count);

bool vulkan_renderer_sampler_create(VulkanContext *context, uint32_t store_index, SamplerDesc description);
bool vulkan_renderer_sampler_destroy(VulkanContext *context, uint32_t retrieve_index);

bool vulkan_renderer_resource_global_create(VulkanContext *context, uint32_t store_index, ResourceBinding *bindings, uint32_t binding_count);
bool vulkan_renderer_resource_global_write(VulkanContext *context, uint32_t retrieve_index, size_t offset, size_t size, void *data);
bool vulkan_renderer_resource_global_bind(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_resource_global_set_texture_sampler(VulkanContext *context, uint32_t retrieve_index, uint32_t binding, uint32_t texture_index, uint32_t sampler_index);

bool vulkan_renderer_resource_group_create(VulkanContext *context, uint32_t store_index, uint32_t shader_index, uint32_t max_instance_count);
bool vulkan_renderer_resource_group_write(VulkanContext *context, uint32_t retrieve_index, uint32_t instance_index, size_t offset, size_t size, void *data, bool all_frames);
bool vulkan_renderer_resource_group_bind(VulkanContext *context, uint32_t retrieve_index, uint32_t instance_index);
bool vulkan_renderer_resource_group_set_texture_sampler(VulkanContext *context, uint32_t retrieve_index, uint32_t binding, uint32_t texture_index, uint32_t sampler_index);

bool vulkan_renderer_resource_local_write(VulkanContext *context, size_t offset, size_t size, void *data);
