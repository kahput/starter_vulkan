#pragma once

#include "vulkan/vk_types.h"

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>
#include <vulkan/vulkan.h>

struct arena;
struct platform;

#define MAX_BUFFERS 1024
#define MAX_TEXTURES 256
#define MAX_SAMPLERS 32
#define MAX_SHADERS 32
#define MAX_PIPELINES 32
#define MAX_RESOURCE_SETS 256

bool vulkan_renderer_create(struct arena *arena, struct platform *platform, VulkanContext *context);
void vulkan_renderer_destroy(VulkanContext *context);

bool vulkan_renderer_begin_frame(VulkanContext *context, struct platform *platform);
bool Vulkan_renderer_end_frame(VulkanContext *context);

bool vulkan_renderer_draw(VulkanContext *context, uint32_t vertex_count);
bool vulkan_renderer_draw_indexed(VulkanContext *context, uint32_t index_count);

bool vulkan_renderer_create_shader(VulkanContext *context, uint32_t store_index, const char *vertex_shader_path, const char *fragment_shader_path);
bool vulkan_renderer_create_pipeline(VulkanContext *context, uint32_t store_index, uint32_t shader_index);
bool vulkan_renderer_create_texture(VulkanContext *context, uint32_t store_index, const Image *image);
bool vulkan_renderer_create_sampler(VulkanContext *context, uint32_t store_index);
bool vulkan_renderer_create_buffer(VulkanContext *context, uint32_t store_index, BufferType type, size_t size, void *data);
bool vulkan_renderer_create_resource_set(VulkanContext *context, uint32_t store_index, uint32_t shader_index, uint32_t set_number);

bool vulkan_renderer_bind_pipeline(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_bind_buffer(VulkanContext *context, uint32_t retrieve_index);
bool vulkan_renderer_bind_resource_set(VulkanContext *context, uint32_t retrieve_index);

bool vulkan_renderer_update_buffer(VulkanContext *context, uint32_t retrieve_index, uint32_t offset, size_t size, void *data);
bool vulkan_renderer_update_resource_set_buffer(VulkanContext *context, uint32_t set_index, const char *name, uint32_t buffer_index);
bool vulkan_renderer_update_resource_set_texture_sampler(VulkanContext *context, uint32_t set_index, const char *name, uint32_t texture_index, uint32_t sampler_index);

// bool vulkan_renderer_set_uniform_buffer(VulkanContext *context, uint32_t shader_index, const char *name, void *data);
// bool vulkan_renderer_set_uniform_texture_sampler(VulkanContext *context, uint32_t shader_index, const char *name, uint32_t texture_index, uint32_t sampler_index);

bool vulkan_create_uniform_buffers(VulkanContext *context, VulkanBuffer *buffer, size_t size);

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

bool vulkan_create_buffer(
	VulkanContext *context,
	uint32_t queue_family_index,
	VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	VkBuffer *buffer, VkDeviceMemory *buffer_memory);
bool vulkan_copy_buffer(VulkanContext *context, VkBuffer src, VkBuffer dst, VkDeviceSize size);

bool vulkan_image_create(VulkanContext *, uint32_t *, uint32_t, uint32_t, uint32_t, VkFormat, VkImageTiling, VkImageUsageFlags, VkMemoryPropertyFlags, VulkanImage *);
bool vulkan_image_view_create(VulkanContext *context, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, VkImageView *view);
bool vulkan_image_transition(VulkanContext *, VkImage, VkImageAspectFlags, VkImageLayout, VkImageLayout, VkPipelineStageFlags, VkPipelineStageFlags, VkAccessFlags, VkAccessFlags);
bool vulkan_buffer_to_image(VulkanContext *context, VkBuffer src, VkImage dst, uint32_t width, uint32_t height);

bool vulkan_create_depth_image(VulkanContext *context);

bool vulkan_begin_single_time_commands(VulkanContext *context, VkCommandPool pool, VkCommandBuffer *buffer);
bool vulkan_end_single_time_commands(VulkanContext *context, VkQueue queue, VkCommandPool pool, VkCommandBuffer *buffer);

void vulkan_load_extensions(VulkanContext *context);

bool vulkan_create_instance(VulkanContext *context, struct platform *platform);
bool vulkan_create_surface(struct platform *platform, VulkanContext *context);

bool vulkan_create_device(struct arena *arena, VulkanContext *context);

bool vulkan_create_swapchain(VulkanContext *context, struct platform *platform);
bool vulkan_recreate_swapchain(VulkanContext *context, struct platform *platform);

bool vulkan_create_renderpass(VulkanContext *context);

bool vulkan_create_descriptor_set_layout(VulkanContext *context, VkDescriptorSetLayoutBinding *bindings, uint32_t binding_count, VkDescriptorSetLayout *out_layout);

bool vulkan_create_command_pool(VulkanContext *context);

bool vulkan_create_descriptor_pool(VulkanContext *context);
// bool vulkan_create_descriptor_set(VulkanContext *context, VulkanShader *shader);

bool vulkan_create_command_buffer(VulkanContext *context);
bool vulkan_create_sync_objects(VulkanContext *context);

// EXTENSIONS
#define VK_CREATE_UTIL_DEBUG_MESSENGER(name) \
	VKAPI_ATTR VkResult VKAPI_CALL name(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pMessenger)
VK_CREATE_UTIL_DEBUG_MESSENGER(vulkan_create_utils_debug_messneger_default);

typedef VK_CREATE_UTIL_DEBUG_MESSENGER(fn_create_utils_debug_messenger);
static fn_create_utils_debug_messenger *vkCreateDebugUtilsMessenger = vulkan_create_utils_debug_messneger_default;
