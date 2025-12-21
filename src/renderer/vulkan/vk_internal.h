#include "vk_types.h"

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>
#include <vulkan/vulkan.h>

struct platform;
struct arena;

bool vulkan_create_uniform_buffers(struct vulkan_context *ctx, VulkanBuffer *buffer, size_t size, void *data);

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

bool vulkan_create_buffer(
	struct vulkan_context *ctx,
	uint32_t queue_family_index,
	VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	VkBuffer *buffer, VkDeviceMemory *buffer_memory);
bool vulkan_copy_buffer(struct vulkan_context *ctx, VkBuffer src, VkBuffer dst, VkDeviceSize size);

bool vulkan_image_create(struct vulkan_context *ctx, uint32_t *, uint32_t, uint32_t, uint32_t, VkFormat, VkImageTiling, VkImageUsageFlags, VkMemoryPropertyFlags, VulkanImage *);
bool vulkan_image_view_create(struct vulkan_context *ctx, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, VkImageView *view);
bool vulkan_image_transition(struct vulkan_context *ctx, VkImage, VkImageAspectFlags, VkImageLayout, VkImageLayout, VkPipelineStageFlags, VkPipelineStageFlags, VkAccessFlags, VkAccessFlags);
bool vulkan_buffer_to_image(struct vulkan_context *ctx, VkBuffer src, VkImage dst, uint32_t width, uint32_t height);

bool vulkan_create_depth_image(struct vulkan_context *ctx);

bool vulkan_begin_single_time_commands(struct vulkan_context *ctx, VkCommandPool pool, VkCommandBuffer *buffer);
bool vulkan_end_single_time_commands(struct vulkan_context *ctx, VkQueue queue, VkCommandPool pool, VkCommandBuffer *buffer);

void vulkan_load_extensions(struct vulkan_context *ctx);

bool vulkan_create_instance(struct vulkan_context *ctx, struct platform *platform);
bool vulkan_create_surface(struct platform *platform, struct vulkan_context *ctx);

bool vulkan_create_device(struct arena *arena, struct vulkan_context *ctx);

bool vulkan_create_swapchain(struct vulkan_context *ctx, uint32_t width, uint32_t height);
bool vulkan_recreate_swapchain(struct vulkan_context *ctx, uint32_t width, uint32_t height);

bool vulkan_create_descriptor_set_layout(struct vulkan_context *ctx, VkDescriptorSetLayoutBinding *bindings, uint32_t binding_count, VkDescriptorSetLayout *out_layout);

bool vulkan_create_command_pool(struct vulkan_context *ctx);

bool vulkan_create_descriptor_pool(struct vulkan_context *ctx);
// bool vulkan_create_descriptor_set(VulkanContext *context, VulkanShader *shader);

bool vulkan_create_global_set(struct vulkan_context *ctx);
bool vulkan_create_command_buffer(struct vulkan_context *ctx);
bool vulkan_create_sync_objects(struct vulkan_context *ctx);

// EXTENSIONS
#define VK_CREATE_UTIL_DEBUG_MESSENGER(name) \
	VKAPI_ATTR VkResult VKAPI_CALL name(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pMessenger)
VK_CREATE_UTIL_DEBUG_MESSENGER(vulkan_create_utils_debug_messneger_default);

typedef VK_CREATE_UTIL_DEBUG_MESSENGER(fn_create_utils_debug_messenger);
static fn_create_utils_debug_messenger *vkCreateDebugUtilsMessenger = vulkan_create_utils_debug_messneger_default;

// EXTENSIONS
#define VK_DESTROY_UTIL_DEBUG_MESSENGER(name) \
	VKAPI_ATTR void VKAPI_CALL name(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks *pAllocator)
VK_DESTROY_UTIL_DEBUG_MESSENGER(vulkan_destroy_utils_debug_messneger_default);

typedef VK_DESTROY_UTIL_DEBUG_MESSENGER(fn_destroy_utils_debug_messenger);
static fn_destroy_utils_debug_messenger *vkDestroyDebugUtilsMessenger = vulkan_destroy_utils_debug_messneger_default;
