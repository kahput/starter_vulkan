#pragma once

#include "common.h"
#include "core/identifiers.h"
#include "renderer/renderer_types.h"

#include <vulkan/vulkan_core.h>

#define MAX_FRAMES_IN_FLIGHT 2
#define SWAPCHAIN_BUFFERING 3

typedef struct vulkan_buffer {
	VkBuffer handle[MAX_FRAMES_IN_FLIGHT];
	VkDeviceMemory memory[MAX_FRAMES_IN_FLIGHT];
	void *mapped[MAX_FRAMES_IN_FLIGHT];

	uint32_t count;
	VkDeviceSize size;

	VkBufferUsageFlags usage;
	VkMemoryPropertyFlags memory_property_flags;
} VulkanBuffer;

typedef struct vulkan_image {
	VkImage handle;
	VkImageView view;
	VkDeviceMemory memory;

	VkFormat format;
} VulkanImage;

typedef struct vulkan_sampler {
	VkSampler handle;
	VkSamplerCreateInfo info;
} VulkanSampler;

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

#define MAX_SETS 3
#define MAX_INPUT_ATTRIBUTES 16
#define MAX_INPUT_BINDINGS 16
#define MAX_PUSH_CONSTANT_RANGES 3
#define MAX_UNIFORMS 32

typedef struct vulkan_shader {
	VkShaderModule vertex_shader, fragment_shader;

	VkVertexInputAttributeDescription attributes[MAX_INPUT_ATTRIBUTES];
	VkVertexInputBindingDescription bindings[MAX_INPUT_BINDINGS];
	uint32_t attribute_count, binding_count;

	VkDescriptorSetLayout layouts[MAX_SETS];
	uint32_t set_count;

	VkPushConstantRange push_constant_ranges[MAX_PUSH_CONSTANT_RANGES];
	uint32_t ps_count;

	ShaderUniform uniforms[MAX_UNIFORMS];
	uint32_t uniform_count;

	VkPipelineLayout pipeline_layout;
} VulkanShader;

typedef struct vulkan_pipeline {
	VkPipeline handle;
	uint32_t shader_index;
} VulkanPipeline;

typedef struct vulkan_resource_set {
	VkDescriptorSet sets[MAX_FRAMES_IN_FLIGHT];
	uint32_t shader_index, set_number;
} VulkanResourceSet;

struct vulkan_context {
	VkInstance instance;

	VkSurfaceKHR surface;
	VulkanDevice device;

	VulkanSwapchain swapchain;
	VulkanImage depth_attachment;
	VkCommandPool graphics_command_pool, transfer_command_pool;
	VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

	VulkanShader *shader_pool;
	VulkanPipeline *pipeline_pool;

	VulkanBuffer *buffer_pool;
	VulkanImage *texture_pool;
	VulkanSampler *sampler_pool;
	VulkanResourceSet *set_pool;

	VkDescriptorPool descriptor_pool;

	VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore render_finished_semaphores[MAX_FRAMES_IN_FLIGHT];
	VkFence in_flight_fences[MAX_FRAMES_IN_FLIGHT];

	uint32_t current_frame;

#ifndef NDEBUG
	VkDebugUtilsMessengerEXT debug_messenger;
#endif
};
