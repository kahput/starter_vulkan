#pragma once

#include "renderer_types.h"
#include "vulkan/vk_types.h"

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>
#include <vulkan/vulkan.h>

struct arena;
struct platform;

bool vulkan_renderer_create(struct arena *arena, struct platform *platform, VulkanContext *context);
void vulkan_renderer_destroy(VulkanContext *context);

bool vulkan_renderer_begin_frame(VulkanContext *context, struct platform *platform);
bool Vulkan_renderer_end_frame(VulkanContext *context);

bool vulkan_renderer_draw(VulkanContext *context, Buffer *vertex_buffer);
bool vulkan_renderer_draw_indexed(VulkanContext *context, Buffer *vertex_buffer, Buffer *index_buffer);

Buffer *vulkan_buffer_create(struct arena *arena, VulkanContext *context, BufferType type, size_t size, void *data);
bool vulkan_buffer_bind(VulkanContext *context, Buffer *buffer);

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

bool vulkan_create_descriptor_set_layout(VulkanContext *context);
bool vulkan_create_pipline(VulkanContext *context, const char *vertex_shader_path, const char *fragment_shader_path, VertexAttribute *attributes, uint32_t attribute_count);

bool vulkan_create_command_pool(VulkanContext *context);

bool vulkan_create_texture_image(VulkanContext *context, uint32_t width, uint32_t height, uint32_t channels, const void *pixels);
bool vulkan_create_texture_image_view(VulkanContext *context);
bool vulkan_create_texture_sampler(VulkanContext *context);

bool vulkan_create_uniform_buffers(VulkanContext *context);
bool vulkan_create_descriptor_pool(VulkanContext *context);
bool vulkan_create_descriptor_set(VulkanContext *context);

bool vulkan_create_command_buffer(VulkanContext *context);
bool vulkan_create_sync_objects(VulkanContext *context);

// EXTENSIONS
#define VK_CREATE_UTIL_DEBUG_MESSENGER(name) \
	VKAPI_ATTR VkResult VKAPI_CALL name(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pMessenger)
VK_CREATE_UTIL_DEBUG_MESSENGER(vulkan_create_utils_debug_messneger_default);

typedef VK_CREATE_UTIL_DEBUG_MESSENGER(fn_create_utils_debug_messenger);
static fn_create_utils_debug_messenger *vkCreateDebugUtilsMessenger = vulkan_create_utils_debug_messneger_default;
