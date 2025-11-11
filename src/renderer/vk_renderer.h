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

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

bool vk_create_buffer(VulkanContext *, uint32_t, VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags, VkBuffer *, VkDeviceMemory *);
bool vk_copy_buffer(VulkanContext *vk_state, VkBuffer src, VkBuffer dst, VkDeviceSize size);

bool vk_image_create(VulkanContext *, uint32_t *, uint32_t, uint32_t, uint32_t, VkFormat, VkImageTiling, VkImageUsageFlags, VkMemoryPropertyFlags, VkImage *, VkDeviceMemory *);
bool vk_image_view_create(VulkanContext *vk_state, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, VkImageView *view);
bool vk_image_layout_transition(VulkanContext *vk_state, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout);
bool vk_buffer_to_image(VulkanContext *vk_state, VkBuffer src, VkImage dst, uint32_t width, uint32_t height);

bool vk_begin_single_time_commands(VulkanContext *vk_state, VkCommandPool pool, VkCommandBuffer *buffer);
bool vk_end_single_time_commands(VulkanContext *vk_state, VkQueue queue, VkCommandPool pool, VkCommandBuffer *buffer);

void vk_load_extensions(VulkanContext *vk_state);

bool vk_create_instance(VulkanContext *vk_state, struct platform *platform);
bool vk_create_surface(struct platform *platform, VulkanContext *vk_state);

bool vk_create_device(struct arena *arena, VulkanContext *vk_state);

bool vk_create_swapchain(VulkanContext *vk_state, struct platform *platform);
bool vk_create_render_pass(VulkanContext *vk_state);
bool vk_create_descriptor_set_layout(VulkanContext *vk_state);
bool vk_create_graphics_pipline(VulkanContext *vk_state);
bool vk_create_framebuffers(VulkanContext *vk_state);
bool vk_recreate_swapchain(VulkanContext *vk_state, struct platform *platform);

bool vk_create_depth_resources(VulkanContext *vk_state);

bool vk_create_command_pool(VulkanContext *vk_state);

bool vk_create_texture_image(VulkanContext *vk_state);
bool vk_create_texture_image_view(VulkanContext *vk_state);
bool vk_create_texture_sampler(VulkanContext *vk_state);

bool vk_create_vertex_buffer(VulkanContext *vk_state);
bool vk_create_index_buffer(VulkanContext *vk_state);

bool vk_create_uniform_buffers(VulkanContext *vk_state);
bool vk_create_descriptor_pool(VulkanContext *vk_state);
bool vk_create_descriptor_set(VulkanContext *vk_state);

bool vk_create_command_buffer(VulkanContext *vk_state);
bool vk_create_sync_objects(VulkanContext *vk_state);

bool vk_record_command_buffers(VulkanContext *vk_state, uint32_t image_index);

// EXTENSIONS
#define VK_CREATE_UTIL_DEBUG_MESSENGER(name) \
	VKAPI_ATTR VkResult VKAPI_CALL name(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pMessenger)
VK_CREATE_UTIL_DEBUG_MESSENGER(vk_create_utils_debug_messneger_default);

typedef VK_CREATE_UTIL_DEBUG_MESSENGER(fn_create_utils_debug_messenger);
static fn_create_utils_debug_messenger *vkCreateDebugUtilsMessenger = vk_create_utils_debug_messneger_default;
