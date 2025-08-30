#pragma once

#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <vulkan/vulkan_core.h>

#define array_count(array) sizeof(array) / sizeof(*array)

struct platform;
struct arena;

typedef struct {
	VkInstance instance;

	VkPhysicalDevice physical_device;
	VkDevice logical_device;
	VkQueue graphics_queue, present_queue;

	VkSurfaceKHR surface;

	VkSwapchainKHR swapchain;

	VkImage *swapchain_images;
	uint32_t swapchain_images_count;

	VkSurfaceFormatKHR swapchain_format;
	VkPresentModeKHR swapchain_present_mode;
	VkExtent2D swapchain_extent;

	VkImageView *image_views;
	uint32_t image_views_count;

	VkFramebuffer *framebuffers;
	uint32_t framebuffer_count;

	VkRenderPass render_pass;
	VkPipelineLayout pipeline_layout;
	VkPipeline graphics_pipeline;

	VkCommandPool command_pool;
	VkCommandBuffer command_buffer;

	VkSemaphore image_available_semaphore;
	VkSemaphore render_finished_semaphore;
	VkFence in_flight_fence;

#ifndef NDEBUG
	VkDebugUtilsMessengerEXT debug_messenger;
#endif
} VKRenderer;

typedef struct {
	int32_t graphic_family;
	int32_t present_family;
} QueueFamilyIndices;

QueueFamilyIndices find_queue_families(struct arena *arena, VKRenderer *renderer);
bool query_swapchain_support(struct arena *arena, VkPhysicalDevice physical_device, VkSurfaceKHR surface);

bool vk_create_instance(struct arena *arena, VKRenderer *renderer, struct platform *platform);
void vk_load_extensions(VKRenderer *renderer);

bool vk_create_surface(struct platform *platform, VKRenderer *renderer);

bool vk_select_physical_device(struct arena *arena, VKRenderer *renderer);
bool vk_create_logical_device(struct arena *arena, VKRenderer *renderer);
bool vk_create_swapchain(struct arena *arena, VKRenderer *renderer, struct platform *platform);
bool vk_create_image_views(struct arena *arena, VKRenderer *renderer);
bool vk_create_render_pass(VKRenderer *renderer);
bool vk_create_graphics_pipline(struct arena *arena, VKRenderer *renderer);
bool vk_create_framebuffers(struct arena *arena, VKRenderer *renderer);
bool vk_create_command_pool(struct arena *arena, VKRenderer *renderer);
bool vk_create_command_buffer(VKRenderer *renderer);
bool vk_create_sync_objects(VKRenderer *renderer);

bool vk_record_command_buffer( uint32_t image_index, VKRenderer *renderer);

// EXTENSIONS
#define VK_CREATE_UTIL_DEBUG_MESSENGER(name) \
	VKAPI_ATTR VkResult VKAPI_CALL name(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pMessenger)
VK_CREATE_UTIL_DEBUG_MESSENGER(vk_create_utils_debug_messneger_default);

typedef VK_CREATE_UTIL_DEBUG_MESSENGER(fn_create_utils_debug_messenger);
static fn_create_utils_debug_messenger *vkCreateDebugUtilsMessenger = vk_create_utils_debug_messneger_default;
