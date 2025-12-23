#pragma once

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>
#include <vulkan/vulkan_core.h>

#include "renderer/backend/vulkan_api.h"

#include "common.h"
#include "allocators/arena.h"

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
#define MAX_DESCRIPTOR_BINDINGS 32
#define MAX_PUSH_CONSTANT_RANGES 3
#define MAX_UNIFORMS 32
#define MAX_VARIANTS 8

typedef struct vulkan_resource_set {
	VkDescriptorSet sets[MAX_FRAMES_IN_FLIGHT];

	VkDescriptorSetLayoutBinding bindings[MAX_DESCRIPTOR_BINDINGS];
	uint32_t binding_count;
} VulkanResourceSet;

typedef struct vulkan_pipeline {
	VkPipeline handle;
	PipelineDesc description;
} VulkanPipeline;

typedef struct vulkan_shader {
	VkShaderModule vertex_shader, fragment_shader;

	VkVertexInputAttributeDescription attributes[MAX_INPUT_ATTRIBUTES];
	VkVertexInputBindingDescription bindings[MAX_INPUT_BINDINGS];
	uint32_t attribute_count, binding_count;

	VkDescriptorSetLayout material_set_layout;
	VulkanResourceSet material_sets[MAX_RESOURCE_SETS];

	VkPushConstantRange push_constant_ranges[MAX_PUSH_CONSTANT_RANGES];
	uint32_t ps_count;

	VkPipelineLayout pipeline_layout;

	VulkanPipeline variants[MAX_VARIANTS];
	uint32_t variant_count;

} VulkanShader;
struct vulkan_context;

struct platform;
struct arena;

bool vulkan_instance_create(struct vulkan_context *ctx, struct platform *platform);
bool vulkan_surface_create(struct platform *platform, struct vulkan_context *ctx);
bool vulkan_device_create(struct arena *arena, struct vulkan_context *ctx);

bool vulkan_swapchain_create(struct vulkan_context *ctx, uint32_t width, uint32_t height);
bool vulkan_swapchain_recreate(struct vulkan_context *ctx, uint32_t width, uint32_t height);

uint32_t vulkan_memory_type_find(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

bool vulkan_buffer_create(
	struct vulkan_context *ctx,
	uint32_t queue_family_index,
	VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	VkBuffer *buffer, VkDeviceMemory *buffer_memory);
bool vulkan_buffer_to_buffer(struct vulkan_context *ctx, VkBuffer src, VkBuffer dst, VkDeviceSize size);
bool vulkan_buffer_to_image(struct vulkan_context *ctx, VkBuffer src, VkImage dst, uint32_t width, uint32_t height);
bool vulkan_buffer_ubo_create(struct vulkan_context *ctx, VulkanBuffer *buffer, size_t size, void *data);

bool vulkan_image_create(struct vulkan_context *ctx, uint32_t *, uint32_t, uint32_t, uint32_t, VkFormat, VkImageTiling, VkImageUsageFlags, VkMemoryPropertyFlags, VulkanImage *);
bool vulkan_image_view_create(struct vulkan_context *ctx, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, VkImageView *view);
bool vulkan_image_transition(struct vulkan_context *ctx, VkImage, VkImageAspectFlags, VkImageLayout, VkImageLayout, VkPipelineStageFlags, VkPipelineStageFlags, VkAccessFlags, VkAccessFlags);

bool vulkan_create_depth_image(struct vulkan_context *ctx);

bool vulkan_command_pool_create(struct vulkan_context *ctx);
bool vulkan_command_buffer_create(struct vulkan_context *ctx);
bool vulkan_command_oneshot_begin(struct vulkan_context *ctx, VkCommandPool pool, VkCommandBuffer *oneshot);
bool vulkan_command_oneshot_end(struct vulkan_context *ctx, VkQueue queue, VkCommandPool pool, VkCommandBuffer *oneshot);

void vulkan_load_extensions(struct vulkan_context *ctx);

bool vulkan_descriptor_pool_create(struct vulkan_context *ctx);
bool vulkan_descriptor_layout_create(struct vulkan_context *ctx, VkDescriptorSetLayoutBinding *bindings, uint32_t binding_count, VkDescriptorSetLayout *out_layout);
bool vulkan_descriptor_global_create(struct vulkan_context *ctx);
// bool vulkan_create_descriptor_set(VulkanContext *context, VulkanShader *shader);

bool vulkan_sync_objects_create(struct vulkan_context *ctx);

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

struct vulkan_context {
	VkInstance instance;

	VkSurfaceKHR surface;
	VulkanDevice device;

	VulkanSwapchain swapchain;
	VulkanImage depth_attachment;
	VkCommandPool graphics_command_pool, transfer_command_pool;
	VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

	VkDescriptorSetLayout globa_set_layout;
	VulkanResourceSet global_set;

	VulkanShader *shader_pool;
	VulkanBuffer *buffer_pool;
	VulkanImage *image_pool;
	VulkanSampler *sampler_pool;

	// TODO: Make backend handle indices
	// IndexRecycler shader_indices;
	// IndexRecycler buffer_indices;
	// IndexRecycler image_indices;
	// IndexRecycler sampler_indices;

	VkDescriptorPool descriptor_pool;

	VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore render_finished_semaphores[MAX_FRAMES_IN_FLIGHT];
	VkFence in_flight_fences[MAX_FRAMES_IN_FLIGHT];

	uint32_t current_frame;

#ifndef NDEBUG
	VkDebugUtilsMessengerEXT debug_messenger;
#endif
};
