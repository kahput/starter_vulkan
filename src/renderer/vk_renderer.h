#pragma once

#include "vulkan/vk_types.h"

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>
#include <vulkan/vulkan.h>

#define array_count(array) sizeof(array) / sizeof(*array)

#define MAX_FRAMES_IN_FLIGHT 3
#define MAX_SWAPCHAIN_IMAGES 3

struct platform;

typedef struct {
	vec3 position;
	vec2 uv;
} Vertex;

extern const Vertex vertices[36];
extern const uint16_t indices[6];

typedef enum {
	FORMAT_FLOAT,
	FORMAT_FLOAT2,
	FORMAT_FLOAT3,
	FORMAT_FLOAT4,

	FORMAT_COUNT
} VertexAttributeFormat;

typedef struct {
	const char *name;
	VertexAttributeFormat format;
} VertexAttribute;

typedef struct {
	mat4 model;
	mat4 view;
	mat4 projection;
} MVPObject;

typedef struct {
	VkInstance instance;
	VulkanDevice device;

	VkSurfaceKHR surface;

	struct {
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
	} swapchain;

	VkImage depth_image;
	VkDeviceMemory depth_image_memory;
	VkImageView depth_image_view;

	VkRenderPass render_pass;
	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;
	VkPipeline graphics_pipeline;

	uint32_t current_frame;

	VkCommandPool graphics_command_pool, transfer_command_pool;
	VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

	VkBuffer vertex_buffer;
	VkDeviceMemory vertex_buffer_memory;

	VkBuffer index_buffer;
	VkDeviceMemory index_buffer_memory;

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
} VulkanState;

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

bool vk_create_buffer(VulkanState *, QueueFamilyIndices, VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags, VkBuffer *, VkDeviceMemory *);
bool vk_copy_buffer(VulkanState *vk_state, VkBuffer src, VkBuffer dst, VkDeviceSize size);

bool vk_image_create(VulkanState *, uint32_t *, uint32_t, uint32_t, uint32_t, VkFormat, VkImageTiling, VkImageUsageFlags, VkMemoryPropertyFlags, VkImage *, VkDeviceMemory *);
bool vk_image_view_create(VulkanState *vk_state, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, VkImageView *view);
bool vk_image_layout_transition(VulkanState *vk_state, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout);
bool vk_buffer_to_image(VulkanState *vk_state, VkBuffer src, VkImage dst, uint32_t width, uint32_t height);

bool vk_begin_single_time_commands(VulkanState *vk_state, VkCommandPool pool, VkCommandBuffer *buffer);
bool vk_end_single_time_commands(VulkanState *vk_state, VkQueue queue, VkCommandPool pool, VkCommandBuffer *buffer);

void vk_load_extensions(VulkanState *vk_state);

bool vk_create_instance(VulkanState *vk_state, struct platform *platform);
bool vk_create_surface(struct platform *platform, VulkanState *vk_state);

bool vk_create_device(VulkanState *vk_state);

bool vk_create_swapchain(VulkanState *vk_state, struct platform *platform);
bool vk_create_render_pass(VulkanState *vk_state);
bool vk_create_descriptor_set_layout(VulkanState *vk_state);
bool vk_create_graphics_pipline(VulkanState *vk_state);
bool vk_create_framebuffers(VulkanState *vk_state);
bool vk_recreate_swapchain(VulkanState *vk_state, struct platform *platform);

bool vk_create_depth_resources(VulkanState *vk_state);

bool vk_create_command_pool(VulkanState *vk_state);

bool vk_create_texture_image(VulkanState *vk_state);
bool vk_create_texture_image_view(VulkanState *vk_state);
bool vk_create_texture_sampler(VulkanState *vk_state);

bool vk_create_vertex_buffer(VulkanState *vk_state);
bool vk_create_index_buffer(VulkanState *vk_state);

bool vk_create_uniform_buffers(VulkanState *vk_state);
bool vk_create_descriptor_pool(VulkanState *vk_state);
bool vk_create_descriptor_set(VulkanState *vk_state);

bool vk_create_command_buffer(VulkanState *vk_state);
bool vk_create_sync_objects(VulkanState *vk_state);

bool vk_record_command_buffers(VulkanState *vk_state, uint32_t image_index);

// EXTENSIONS
#define VK_CREATE_UTIL_DEBUG_MESSENGER(name) \
	VKAPI_ATTR VkResult VKAPI_CALL name(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pMessenger)
VK_CREATE_UTIL_DEBUG_MESSENGER(vk_create_utils_debug_messneger_default);

typedef VK_CREATE_UTIL_DEBUG_MESSENGER(fn_create_utils_debug_messenger);
static fn_create_utils_debug_messenger *vkCreateDebugUtilsMessenger = vk_create_utils_debug_messneger_default;
