#pragma once

#include "vulkan/vk_types.h"

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>
#include <vulkan/vulkan.h>

struct arena;
struct platform;

typedef struct {
	vec3 position;
	vec2 uv;
} Vertex;

typedef struct buffer {
	uint32_t vertex_count, index_count;

	void *internal;
} Buffer;

typedef enum buffer_type {
	BUFFER_TYPE_VERTEX,
	BUFFER_TYPE_INDEX,
	BUFFER_TYPE_UNIFORM,
} BufferType;

typedef enum vertex_attribute_format {
	FORMAT_FLOAT,
	FORMAT_FLOAT2,
	FORMAT_FLOAT3,
	FORMAT_FLOAT4,

	FORMAT_COUNT
} VertexAttributeFormat;

typedef struct vertex_attribute {
	const char *name;
	VertexAttributeFormat format;
} VertexAttribute;

typedef struct {
	mat4 model;
	mat4 view;
	mat4 projection;
} MVPObject;

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

Buffer *vk_create_buffer(struct arena *arena, VulkanContext *context, BufferType type, size_t size, void *data);
bool create_buffer(
	VulkanContext *context,
	uint32_t queue_family_index,
	VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	VkBuffer *buffer, VkDeviceMemory *buffer_memory);
bool vk_copy_buffer(VulkanContext *context, VkBuffer src, VkBuffer dst, VkDeviceSize size);

bool vk_image_create(VulkanContext *, uint32_t *, uint32_t, uint32_t, uint32_t, VkFormat, VkImageTiling, VkImageUsageFlags, VkMemoryPropertyFlags, VkImage *, VkDeviceMemory *);
bool vk_image_view_create(VulkanContext *context, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, VkImageView *view);
bool vk_image_layout_transition(VulkanContext *context, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout);
bool vk_buffer_to_image(VulkanContext *context, VkBuffer src, VkImage dst, uint32_t width, uint32_t height);

bool vk_begin_single_time_commands(VulkanContext *context, VkCommandPool pool, VkCommandBuffer *buffer);
bool vk_end_single_time_commands(VulkanContext *context, VkQueue queue, VkCommandPool pool, VkCommandBuffer *buffer);

void vk_load_extensions(VulkanContext *context);

bool vk_create_instance(VulkanContext *context, struct platform *platform);
bool vk_create_surface(struct platform *platform, VulkanContext *context);

bool vk_create_device(struct arena *arena, VulkanContext *context);

bool vk_create_swapchain(VulkanContext *context, struct platform *platform);
bool vk_create_render_pass(VulkanContext *context);
bool vk_create_descriptor_set_layout(VulkanContext *context);
bool vk_create_graphics_pipline(VulkanContext *context);
bool vk_create_framebuffers(VulkanContext *context);
bool vk_recreate_swapchain(VulkanContext *context, struct platform *platform);

bool vk_create_depth_resources(VulkanContext *context);

bool vk_create_command_pool(VulkanContext *context);

bool vk_create_texture_image(VulkanContext *context);
bool vk_create_texture_image_view(VulkanContext *context);
bool vk_create_texture_sampler(VulkanContext *context);


bool vk_create_uniform_buffers(VulkanContext *context);
bool vk_create_descriptor_pool(VulkanContext *context);
bool vk_create_descriptor_set(VulkanContext *context);

bool vk_create_command_buffer(VulkanContext *context);
bool vk_create_sync_objects(VulkanContext *context);

bool vk_command_buffer_draw(VulkanContext *context, Buffer *vertex_buffer, uint32_t image_index);

// EXTENSIONS
#define VK_CREATE_UTIL_DEBUG_MESSENGER(name) \
	VKAPI_ATTR VkResult VKAPI_CALL name(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pMessenger)
VK_CREATE_UTIL_DEBUG_MESSENGER(vk_create_utils_debug_messneger_default);

typedef VK_CREATE_UTIL_DEBUG_MESSENGER(fn_create_utils_debug_messenger);
static fn_create_utils_debug_messenger *vkCreateDebugUtilsMessenger = vk_create_utils_debug_messneger_default;
