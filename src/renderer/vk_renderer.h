#pragma once

#include "common.h"
#include "renderer_types.h"

struct arena;
struct platform;

typedef struct vulkan_context VulkanContext;

#define MAX_BUFFERS 2048
#define MAX_TEXTURES 512
#define MAX_SAMPLERS 32
#define MAX_SHADERS 32
#define MAX_PIPELINES 32
#define MAX_RESOURCE_SETS 512

bool vulkan_renderer_create(struct arena *arena, VulkanContext **context, struct platform *platform);
void vulkan_renderer_destroy(VulkanContext *context);
bool vulkan_renderer_resize(VulkanContext *context, uint32_t new_width, uint32_t new_height);

bool vulkan_renderer_begin_frame(VulkanContext *context, struct platform *platform);
bool Vulkan_renderer_end_frame(VulkanContext *context);

bool vulkan_renderer_draw(VulkanContext *context, uint32_t vertex_count);
bool vulkan_renderer_draw_indexed(VulkanContext *context, uint32_t index_count);

bool vulkan_renderer_create_shader(VulkanContext *context, uint32_t store_index, const char *vertex_shader_path, const char *fragment_shader_path);
bool vulkan_renderer_create_pipeline(VulkanContext *context, uint32_t store_index, PipelineDesc description);

bool vulkan_renderer_create_texture(VulkanContext *context, uint32_t store_index, uint32_t width, uint32_t height, uint32_t channels, uint8_t *pixels);
bool vulkan_renderer_create_sampler(VulkanContext *context, uint32_t store_index);
bool vulkan_renderer_create_buffer(VulkanContext *context, uint32_t store_index, BufferType type, size_t size, void *data);
bool vulkan_renderer_create_resource_set(VulkanContext *context, uint32_t store_index, uint32_t shader_index, uint32_t set_number);

bool vulkan_renderer_destroy_shader(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_destroy_pipeline(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_destroy_texture(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_destroy_sampler(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_destroy_buffer(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_destroy_resource_set(VulkanContext *context, uint32_t retrieve_index);

bool vulkan_renderer_push_constants(VulkanContext *context, uint32_t shader_index, const char *name, void *data);

bool vulkan_renderer_bind_pipeline(VulkanContext *context, uint32_t retrieve_index);

bool vulkan_renderer_bind_buffer(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_bind_buffers(VulkanContext *context, uint32_t *buffers, uint32_t count);

bool vulkan_renderer_bind_resource_set(VulkanContext *context, uint32_t retrieve_index);

bool vulkan_renderer_update_buffer(VulkanContext *context, uint32_t retrieve_index, uint32_t offset, size_t size, void *data);
bool vulkan_renderer_update_resource_set_buffer(VulkanContext *context, uint32_t set_index, const char *name, uint32_t buffer_index);
bool vulkan_renderer_update_resource_set_texture_sampler(VulkanContext *context, uint32_t set_index, const char *name, uint32_t texture_index, uint32_t sampler_index);
