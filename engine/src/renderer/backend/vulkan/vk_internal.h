#pragma once

#include "core/arena.h"

#include "core/hash_trie.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include <vulkan/vulkan_core.h>

#define MAX_FRAMES_IN_FLIGHT 2
#define SWAPCHAIN_IMAGE_COUNT 3

#define MAX_SHADER_VARIANTS 8
#define MAX_SETS 2
#define MAX_INPUT_ATTRIBUTES 16
#define MAX_INPUT_BINDINGS 16
#define MAX_BINDINGS_PER_RESOURCE 16
#define MAX_PUSH_CONSTANT_RANGES 3
#define MAX_UNIFORMS 32

typedef enum {
	VULKAN_RESOURCE_STATE_UNINITIALIZED,
	VULKAN_RESOURCE_STATE_INITIALIZED,
} VulkanResourceState;
#define VULKAN_GET_OR_RETURN(ptr_var, pool_array, handle, max_limit, expect_initialized, return_type) \
	do {                                                                                              \
		if ((handle.id) == 0 || (handle.id) >= (max_limit)) {                                         \
			LOG_ERROR("Vulkan: %s index %u out of bounds (min 1, max %u), aborting %s",               \
				#ptr_var, (uint32_t)(handle.id), (uint32_t)(max_limit), __func__);                    \
			return return_type;                                                                       \
		}                                                                                             \
                                                                                                      \
		(ptr_var) = &(pool_array)[handle.id];                                                         \
		if ((expect_initialized)) {                                                                   \
			if ((ptr_var)->state != VULKAN_RESOURCE_STATE_INITIALIZED) {                              \
				LOG_ERROR("Vulkan: %s at index %u is not initialized (State: %d), aborting %s",       \
					#ptr_var, (uint32_t)(handle.id), (ptr_var)->state, __func__);                     \
				ASSERT(false);                                                                        \
				return return_type;                                                                   \
			}                                                                                         \
		} else {                                                                                      \
			if ((ptr_var)->state != VULKAN_RESOURCE_STATE_UNINITIALIZED) {                            \
				LOG_FATAL("Vulkan: %s at index %u is already in use (State: %d), aborting %s",        \
					#ptr_var, (uint32_t)(handle.id), (ptr_var)->state, __func__);                     \
				ASSERT(false);                                                                        \
				return return_type;                                                                   \
			}                                                                                         \
		}                                                                                             \
	} while (0)

typedef struct vulkan_buffer {
	VulkanResourceState state;

	VkBuffer handle;
	VkDeviceMemory memory;
	void *mapped;

	uint32_t count;

	VkDeviceSize required_alignment, stride;
	VkDeviceSize offset, size;

	VkBufferUsageFlags usage;
	VkMemoryPropertyFlags memory_property_flags;
} VulkanBuffer;

typedef struct vulkan_group_resource {
	VulkanResourceState state;

	VulkanBuffer buffer;
	VkDescriptorSet set;

	uint32_t max_instance_count;
	RhiShader shader;
} VulkanGroupResource;

typedef struct vulkan_global_resource {
	VulkanResourceState state;

	VulkanBuffer buffer;

	VkDescriptorSetLayoutBinding bindings[MAX_BINDINGS_PER_RESOURCE];
	uint32_t binding_count;

	VkDescriptorSetLayout set_layout;
	VkDescriptorSet set;

	VkPipelineLayout pipeline_layout;
} VulkanGlobalResource;

typedef struct {
	VulkanResourceState state;

	VkDescriptorSet handle;
	VkDescriptorSetLayout layout;
	uint64_t layout_hash;

	uint32_t number;

	VkPipelineLayout pipeline_layout;

	VulkanBuffer *buffer;
} VulkanUniformSet;

typedef struct vulkan_resource_set {
	VkDescriptorSet sets[MAX_FRAMES_IN_FLIGHT];
} VulkanResourceSet;

typedef struct vulkan_image {
	VulkanResourceState state;

	VkImage handle;
	VkImageView view;
	VkDeviceMemory memory;

	VkImageLayout layout;
	VkImageAspectFlags aspect;
	TextureType type;
	VkImageCreateInfo info;
	uint32_t width, height;
} VulkanImage;

typedef struct vulkan_attachment {
	VulkanResourceState state;
	uint32_t image_index;
	bool present;

	VkAttachmentStoreOp store;
	VkAttachmentLoadOp load;
	VkClearValue clear;
} VulkanAttachment;

typedef struct vulkan_pass {
	VulkanResourceState state;

	VkFormat color_formats[4];
	VkFormat depth_format;

	VkSampleCountFlags sample_count;
} VulkanPass;

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

	VkFormat depth_format;

	int32_t graphics_index, transfer_index, present_index;
	VkQueue graphics_queue, transfer_queue, present_queue;

	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceFeatures features;

	VkSampleCountFlags sample_count;
	bool multi_sample;

} VulkanDevice;

bool vulkan_instance_create(VulkanContext *context, void *display);
bool vulkan_surface_create(VulkanContext *context, void *display);
bool vulkan_device_create(Arena *arena, VulkanContext *context);

typedef struct VulkanSwapchain {
	VkSwapchainKHR handle;

	struct {
		VkImage handles[SWAPCHAIN_IMAGE_COUNT];
		VkImageView views[SWAPCHAIN_IMAGE_COUNT];
		uint32_t count;
	} images;

	VkSurfaceFormatKHR format;
	VkPresentModeKHR present_mode;
	VkExtent2D extent;
} VulkanSwapchain;
bool vulkan_swapchain_create(VulkanContext *context, uint32_t width, uint32_t height);
bool vulkan_swapchain_recreate(VulkanContext *context, uint32_t width, uint32_t height);

bool vulkan_buffer_create(
	VulkanContext *context,
	VkDeviceSize size, uint32_t count, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	VulkanBuffer *out_buffer);

bool vulkan_buffer_memory_map(VulkanContext *context, VulkanBuffer *buffer);
void vulkan_buffer_memory_unmap(VulkanContext *context, VulkanBuffer *buffer);
void vulkan_buffer_write(VulkanBuffer *buffer, size_t offset, size_t size, void *data);
void vulkan_buffer_write_indexed(VulkanBuffer *buffer, uint32_t index, size_t offset, size_t size, void *data);
bool vulkan_buffer_to_buffer(VulkanContext *context, VkDeviceSize src_offset, VkBuffer src, VkDeviceSize dst_offset, VkBuffer dst, VkDeviceSize size);
bool vulkan_buffer_to_image(VulkanContext *context, VkDeviceSize src_offset, VkBuffer src, VkImage dst, uint32_t width, uint32_t height, uint32_t layer_count, VkDeviceSize layer_size);
bool vulkan_buffer_ubo_create(VulkanContext *context, VulkanBuffer *buffer, size_t size, void *data);

// bool vulkan_pass_on_resize(VulkanContext *context, VulkanPass *pass);

bool vulkan_image_create(VulkanContext *context, VkSampleCountFlags, uint32_t, uint32_t, VkFormat, VkImageTiling, VkImageUsageFlags, TextureType, VkMemoryPropertyFlags, VulkanImage *);
void vulkan_image_destroy(VulkanContext *context, VulkanImage *image);

bool vulkan_image_view_create(VulkanContext *context, VkImageViewType type, VkImageAspectFlags aspect_flags, VulkanImage *image);
void vulkan_image_transition_oneshot(VulkanContext *context, VkImage, VkImageAspectFlags, uint32_t, VkImageLayout, VkImageLayout, VkPipelineStageFlags, VkPipelineStageFlags, VkAccessFlags, VkAccessFlags);
void vulkan_image_transition(VulkanContext *context, VkCommandBuffer, VkImage, VkImageAspectFlags, uint32_t, VkImageLayout, VkImageLayout, VkPipelineStageFlags, VkPipelineStageFlags, VkAccessFlags, VkAccessFlags);
void vulkan_image_transition_auto(VulkanImage *image, VkCommandBuffer command_buffer, VkImageLayout new_layout);

bool vulkan_image_msaa_scratch_ensure(VulkanContext *context, VulkanImage *msaa, VkExtent2D extent, VkFormat format, VkSampleCountFlags sample_count, VkImageAspectFlags aspect);

bool vulkan_command_pool_create(VulkanContext *context);
bool vulkan_command_buffer_create(VulkanContext *context);
bool vulkan_command_oneshot_begin(VulkanContext *context, VkCommandPool pool, VkCommandBuffer *oneshot);
bool vulkan_command_oneshot_end(VulkanContext *context, VkQueue queue, VkCommandPool pool, VkCommandBuffer *oneshot);

void vulkan_load_extensions(VulkanContext *context);

bool vulkan_descriptor_pool_create(VulkanContext *context);
bool vulkan_descriptor_layout_create(VulkanContext *context, VkDescriptorSetLayoutBinding *bindings, uint32_t binding_count, VkDescriptorSetLayout *out_layout);
bool vulkan_descriptor_global_create(VulkanContext *context);
// bool vulkan_create_descriptor_set(VulkanContext *context, VulkanShader *shader);

bool vulkan_sync_objects_create(VulkanContext *context);

size_t vulkan_memory_required_alignment(VulkanContext *context, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties);
uint32_t vulkan_memory_type_find(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

VkSampleCountFlags vulkan_utils_max_sample_count(VulkanContext *contxt);

typedef struct vulkan_pipeline {
	HashTrieNode node;

	struct vulkan_pipeline *next;
	struct vulkan_pipeline *prev;

	VkPipeline handle;
} VulkanPipeline;

typedef struct vulkan_shader {
	VulkanResourceState state;
	VkShaderModule vertex_shader, fragment_shader;

	VkVertexInputAttributeDescription attributes[MAX_INPUT_ATTRIBUTES];
	VkVertexInputBindingDescription bindings[MAX_INPUT_BINDINGS];
	uint32_t attribute_count, binding_count;

	VkDescriptorSetLayout layouts[4];

	uint32_t group_ubo_binding;
	VkDeviceSize instance_size;

	VkPipelineLayout pipeline_layout;
	VulkanPipeline *pipeline_root;

	VulkanPipeline *pipeline_lru_head;

	VulkanPipeline variants[MAX_SHADER_VARIANTS];
	uint32_t variant_count;
} VulkanShader;

typedef struct vulkan_sampler {
	VulkanResourceState state;

	VkSampler handle;
	VkSamplerCreateInfo info;
} VulkanSampler;

struct vulkan_context {
	VkInstance instance;
	void *display;

	VkSurfaceKHR surface;
	VulkanDevice device;

	VulkanSwapchain swapchain;

	VulkanImage msaa_colors[MAX_COLOR_ATTACHMENTS];
	VulkanImage msaa_depth;

	VkCommandPool graphics_command_pool, transfer_command_pool;
	VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

	VkPushConstantRange global_range;

	VulkanShader *shader_pool;
	VulkanBuffer *buffer_pool;
	VulkanImage *image_pool;
	VulkanSampler *sampler_pool;
	VulkanUniformSet *set_pool;

	VulkanBuffer staging_buffer;
	VulkanShader *bound_shader;
	VulkanPass bound_pass;
	VkDescriptorPool descriptor_pool;

	VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore render_finished_semaphores[SWAPCHAIN_IMAGE_COUNT];
	VkFence in_flight_fences[MAX_FRAMES_IN_FLIGHT];

	uint32_t current_frame, image_index;

#ifndef NDEBUG
	VkDebugUtilsMessengerEXT debug_messenger;
#endif
};

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
