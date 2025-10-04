#pragma once

#include <cglm/cglm.h>
#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <vulkan/vulkan_core.h>

#define array_count(array) sizeof(array) / sizeof(*array)
#define MAX_FRAMES_IN_FLIGHT 3

struct platform;
struct arena;

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

	VkPhysicalDevice physical_device;
	VkDevice logical_device;
	VkQueue graphics_queue, transfer_queue, present_queue;

	VkSurfaceKHR surface;

	struct {
		VkSwapchainKHR handle;

		uint32_t image_count;
		VkImage *images;

		VkSurfaceFormatKHR format;
		VkPresentModeKHR present_mode;
		VkExtent2D extent;
	} swapchain;

	VkImageView *image_views;
	uint32_t image_views_count;

	VkFramebuffer *framebuffers;
	uint32_t framebuffer_count;

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
} VKRenderer;

typedef struct {
	int32_t graphics;
	int32_t transfer;
	int32_t present;
} QueueFamilyIndices;

QueueFamilyIndices find_queue_families(struct arena *scratch_arena, VKRenderer *renderer);
bool query_swapchain_support(struct arena *arena, VkPhysicalDevice physical_device, VkSurfaceKHR surface);

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

bool vk_create_buffer(VKRenderer *, QueueFamilyIndices, VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags, VkBuffer *, VkDeviceMemory *);
bool vk_copy_buffer(VKRenderer *renderer, VkBuffer src, VkBuffer dst, VkDeviceSize size);

bool vk_create_image(VKRenderer *, QueueFamilyIndices, uint32_t, uint32_t, VkFormat, VkImageTiling, VkImageUsageFlags, VkMemoryPropertyFlags, VkImage *, VkDeviceMemory *);
bool vk_create_image_view(VKRenderer *renderer, VkImage image, VkFormat format, VkImageView *view);
bool vk_transition_image_layout(VKRenderer *renderer, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout);
bool vk_copy_buffer_to_image(VKRenderer *renderer, VkBuffer src, VkImage dst, uint32_t width, uint32_t height);

bool vk_begin_single_time_commands(VKRenderer *renderer, VkCommandPool pool, VkCommandBuffer *buffer);
bool vk_end_single_time_commands(VKRenderer *renderer, VkQueue queue, VkCommandPool pool, VkCommandBuffer *buffer);

void vk_load_extensions(VKRenderer *renderer);

bool vk_create_instance(struct arena *arena, VKRenderer *renderer, struct platform *platform);
bool vk_create_surface(struct platform *platform, VKRenderer *renderer);

bool vk_select_physical_device(struct arena *arena, VKRenderer *renderer);
bool vk_create_logical_device(struct arena *arena, VKRenderer *renderer);

bool vk_create_swapchain(struct arena *arena, VKRenderer *renderer, struct platform *platform);
bool vk_create_swapchain_image_views(struct arena *arena, VKRenderer *renderer);
bool vk_create_render_pass(VKRenderer *renderer);
bool vk_create_descriptor_set_layout(VKRenderer *renderer);
bool vk_create_graphics_pipline(struct arena *arena, VKRenderer *renderer);
bool vk_create_framebuffers(struct arena *arena, VKRenderer *renderer);
bool vk_recreate_swapchain(struct arena *arena, VKRenderer *renderer, struct platform *platform);

bool vk_create_command_pool(struct arena *arena, VKRenderer *renderer);

bool vk_create_texture_image(struct arena *arena, VKRenderer *renderer);
bool vk_create_texture_image_view(struct arena *arena, VKRenderer *renderer);

bool vk_create_vertex_buffer(struct arena *scratch_arena, VKRenderer *renderer);
bool vk_create_index_buffer(struct arena *scratch_arena, VKRenderer *renderer);

bool vk_create_uniform_buffers(struct arena *scratch_arena, VKRenderer *renderer);
bool vk_create_descriptor_pool(VKRenderer *renderer);
bool vk_create_descriptor_set(VKRenderer *renderer);

bool vk_create_command_buffer(VKRenderer *renderer);
bool vk_create_sync_objects(VKRenderer *renderer);

bool vk_record_command_buffers(VKRenderer *renderer, uint32_t image_index);

// EXTENSIONS
#define VK_CREATE_UTIL_DEBUG_MESSENGER(name) \
	VKAPI_ATTR VkResult VKAPI_CALL name(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pMessenger)
VK_CREATE_UTIL_DEBUG_MESSENGER(vk_create_utils_debug_messneger_default);

typedef VK_CREATE_UTIL_DEBUG_MESSENGER(fn_create_utils_debug_messenger);
static fn_create_utils_debug_messenger *vkCreateDebugUtilsMessenger = vk_create_utils_debug_messneger_default;
