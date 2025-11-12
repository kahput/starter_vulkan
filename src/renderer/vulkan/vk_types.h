#pragma once

#include "common.h"

#include <vulkan/vulkan_core.h>

#define MAX_FRAMES_IN_FLIGHT 3
#define MAX_SWAPCHAIN_IMAGES 3

typedef struct vulkan_buffer {
	VkBuffer handle;
	VkBufferUsageFlags usage;

	VkDeviceMemory memory;
	VkMemoryPropertyFlags memory_property_flags;

} VulkanBuffer;

typedef struct vulkan_image {
	VkImage handle;
	VkDeviceMemory memory;
	VkImageView view;
} VulkanImage;

typedef struct swapchain_support_details {
	VkSurfaceCapabilitiesKHR capabilities;

	VkSurfaceFormatKHR *formats;
	uint32_t format_count;

	VkPresentModeKHR *present_modes;
	uint32_t present_mode_count;
} SwapchainSupportDetails;

typedef struct vulkan_device {
	VkPhysicalDevice physical;
	VkDevice logical;
	SwapchainSupportDetails swapchain_details;

	int32_t graphics_index, transfer_index, present_index;
	VkQueue graphics_queue, transfer_queue, present_queue;

	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceFeatures features;

} VulkanDevice;

typedef struct VulkanSwapchain {
	VkSwapchainKHR handle;

	uint32_t image_count;
	VkImage images[MAX_SWAPCHAIN_IMAGES];

	VkImageView image_views[MAX_SWAPCHAIN_IMAGES];
	uint32_t image_views_count;

	VkFramebuffer framebuffers[MAX_SWAPCHAIN_IMAGES];
	uint32_t framebuffer_count;

	VkSurfaceFormatKHR format;
	VkPresentModeKHR present_mode;
	VkExtent2D extent;
} VulkanSwapchain;

typedef struct {
	VkInstance instance;

	VkSurfaceKHR surface;
	VulkanDevice device;

	VulkanSwapchain swapchain;
	VulkanImage depth_attachment;

	VkRenderPass render_pass;
	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;
	VkPipeline graphics_pipeline;

	uint32_t current_frame;

	VkCommandPool graphics_command_pool, transfer_command_pool;
	VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];


	VkImage texture_image;
	VkDeviceMemory texture_image_memory;
	VkImageView texture_image_view;
	VkSampler texture_sampler;

	VkBuffer uniform_buffers[MAX_FRAMES_IN_FLIGHT];
	VkDeviceMemory uniform_buffers_memory[MAX_FRAMES_IN_FLIGHT];
	void *uniform_buffers_mapped[MAX_FRAMES_IN_FLIGHT];

	VkDescriptorPool descriptor_pool;
	VkDescriptorSet descriptor_sets[MAX_FRAMES_IN_FLIGHT];

	VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore render_finished_semaphores[MAX_FRAMES_IN_FLIGHT];
	VkFence in_flight_fences[MAX_FRAMES_IN_FLIGHT];

#ifndef NDEBUG
	VkDebugUtilsMessengerEXT debug_messenger;
#endif
} VulkanContext;
