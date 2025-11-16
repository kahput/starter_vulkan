#pragma once

#include "vulkan/vk_types.h"

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>
#include <vulkan/vulkan.h>

struct arena;
struct platform;

typedef struct {
	vec3 position;
	vec3 normal;
	vec2 uv;
	vec4 tangent;
} Vertex;

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
	uint8_t binding;
} VertexAttribute;

typedef struct buffer {
	uint32_t vertex_count, index_count;

	void *internal;
} Buffer;

typedef struct {
	mat4 model;
	mat4 view;
	mat4 projection;
} MVPObject;

bool vulkan_renderer_create(struct arena *arena, struct platform *platform, VulkanContext *context);
void vulkan_renderer_destroy(VulkanContext *context);

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

Buffer *vulkan_create_buffer(struct arena *arena, VulkanContext *context, BufferType type, size_t size, void *data);
bool create_buffer(
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
bool vulkan_create_pipline(VulkanContext *context, VertexAttribute *attributes, uint32_t attribute_count);

bool vulkan_create_command_pool(VulkanContext *context);

bool vulkan_create_texture_image(VulkanContext *context, const char *file_path);
bool vulkan_create_texture_image_view(VulkanContext *context);
bool vulkan_create_texture_sampler(VulkanContext *context);

bool vulkan_create_uniform_buffers(VulkanContext *context);
bool vulkan_create_descriptor_pool(VulkanContext *context);
bool vulkan_create_descriptor_set(VulkanContext *context);

bool vulkan_create_command_buffer(VulkanContext *context);
bool vulkan_create_sync_objects(VulkanContext *context);

bool vulkan_command_buffer_draw(VulkanContext *context, Buffer *vertex_buffer, Buffer *index_buffer, uint32_t image_index);

// EXTENSIONS
#define VK_CREATE_UTIL_DEBUG_MESSENGER(name) \
	VKAPI_ATTR VkResult VKAPI_CALL name(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pMessenger)
VK_CREATE_UTIL_DEBUG_MESSENGER(vulkan_create_utils_debug_messneger_default);

typedef VK_CREATE_UTIL_DEBUG_MESSENGER(fn_create_utils_debug_messenger);
static fn_create_utils_debug_messenger *vkCreateDebugUtilsMessenger = vulkan_create_utils_debug_messneger_default;
