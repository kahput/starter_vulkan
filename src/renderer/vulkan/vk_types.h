#pragma once

#include "common.h"
#include "core/identifiers.h"

#include <vulkan/vulkan_core.h>

#define MAX_FRAMES_IN_FLIGHT 2
#define SWAPCHAIN_BUFFERING 3

typedef struct vulkan_buffer {
	VkBuffer handle;
	VkDeviceMemory memory;
	void *mapped;

	VkBufferUsageFlags usage;
	VkMemoryPropertyFlags memory_property_flags;
} VulkanBuffer;

typedef struct vulkan_image {
	VkImage handle;
	VkFormat format;

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

	struct {
		VkImage handles[SWAPCHAIN_BUFFERING];
		VkImageView views[SWAPCHAIN_BUFFERING];
		uint32_t count;
	} images;

	VkSurfaceFormatKHR format;
	VkPresentModeKHR present_mode;
	VkExtent2D extent;
} VulkanSwapchain;

typedef struct vulkan_shader {
	VkShaderModule vertex_shader, fragment_shader;

	VkVertexInputBindingDescription *bindings;
	uint32_t binding_count;

	VkVertexInputAttributeDescription *attributes;
	uint32_t attribute_count;

	VkDescriptorSetLayout *layouts;
	uint32_t layout_count;

	VkPipelineLayout pipeline_layout;
} VulkanShader;

typedef struct {
	VkInstance instance;

	VkSurfaceKHR surface;
	VulkanDevice device;

	VulkanSwapchain swapchain;
	VulkanImage depth_attachment;

	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;

	VulkanShader shader;
	VkPipeline graphics_pipeline;

	uint32_t current_frame;

	VkCommandPool graphics_command_pool, transfer_command_pool;
	VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

	VulkanImage texture_image;
	VkSampler texture_sampler;

	struct pool *buffer_pool;

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
