#include "common.h"
#include "core/arena.h"
#include "core/input_types.h"
#include "core/logger.h"
#include "core/debug.h"
#include "os.h"
#include <vulkan/vulkan_core.h>

#include "vulkan/vulkan.h"

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct {
	VkImage handle;
	VkImageView view;
	VkDeviceMemory memory;

	uint32_t width, height;

	VkImageCreateInfo image_info;
	VkImageViewCreateInfo view_info;
} GFX_Image;

typedef struct {
	VkBuffer handle;
	VkDeviceMemory memory;

	uint64_t size;

	VkBufferCreateInfo info;
} GFX_Buffer;

typedef struct {
	VkInstance instance;

	VkPhysicalDevice physical_device;
	VkDevice logical_device;

	// Queues
	int32_t graphics_index, present_index;
	int32_t transfer_index, compute_index;

	VkQueue graphics_queue, present_queue;

	// Commands
	VkCommandPool graphics_command_pool;
	VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

	// Syncrhonization primitives
	/* VkSemaphore image_available_semaphores[MAX_FRAMES_IN_FLIGHT]; */
	/* VkSemaphore render_finished_semaphores[SWAPCHAIN_IMAGE_COUNT]; */
	VkFence in_flight_fences[MAX_FRAMES_IN_FLIGHT];

	// transient frame descriptor pools
	VkDescriptorPool frame_descriptor_pools[MAX_FRAMES_IN_FLIGHT];

	// engine push constant range
	VkPushConstantRange global_range;

	// Running
	uint32_t current_frame;

#ifndef BUILD_DIST
	VkDebugUtilsMessengerEXT debug_messenger;
#endif
} GFX_Context;
bool gfx_startup(GFX_Context *context);
void gfx_shutdown(GFX_Context *context);

typedef enum {
	PIXEL_FORMAT_RGBA8_UNORM,
	PIXEL_FORMAT_RGBA8_SRGB,
	PIXEL_FORMAT_RGBA16_FLOAT,
	PIXEL_FORMAT_R32_FLOAT,
	PIXEL_FORMAT_D32_FLOAT,
	PIXEL_FORMAT_D24_UNORM_S8_UINT,
} PixelFormat;

typedef enum {
	IMAGE_USAGE_SAMPLE = 0x1,
	IMAGE_USAGE_RENDER = 0x2,
	IMAGE_USAGE_STORAGE = 0x4,
	IMAGE_USAGE_TRANSFER = 0x8
} ImageUsageFlags;

typedef enum {
	BUFFER_USAGE_VERTEX = 0x1,
	BUFFER_USAGE_INDEX = 0x2,
	BUFFER_USAGE_UNIFORM = 0x4,
	BUFFER_USAGE_STORAGE = 0x8,
	BUFFER_USAGE_TRANSFER = 0x10
} BufferUsage;

typedef enum {
	BUFFER_MEMORY_LOCAl,
	BUFFER_MEMORY_SHARED,
} BufferMemory;
GFX_Image vulkan_image_make(GFX_Context *context, uint32_t width, uint32_t height, PixelFormat format, ImageUsageFlags usage);
GFX_Buffer vulkan_buffer_make(GFX_Context *context, uint64_t size, BufferMemory memory, BufferUsage usage);

int main(void) {
	logger_set_level(LOG_LEVEL_DEBUG);

	os_display_startup();
	OS_Surface *window = os_surface_open(1280, 720, s("vulkan_scratch"), 0);

	uint64_t start_time = os_time_ns();

	ArenaTemp scratch = arena_scratch_begin(0);

	GFX_Context context[1] = { 0 };
	gfx_startup(context);

	GFX_Image image = vulkan_image_make(context, 256, 256, PIXEL_FORMAT_RGBA8_UNORM, IMAGE_USAGE_RENDER | IMAGE_USAGE_TRANSFER);
	GFX_Buffer output_buffer = vulkan_buffer_make(context, 256 * 256 * 4, BUFFER_MEMORY_SHARED, BUFFER_USAGE_TRANSFER);
	void *buffer_data = NULL;
	vkMapMemory(context->logical_device, output_buffer.memory, 0, output_buffer.size, 0, &buffer_data);

	bool is_open = true;
	while (is_open) {
		OS_Event event = { 0 };
		while (os_event_poll(&event)) {
			switch (event.type) {
				case OS_EVENT_TYPE_SURFACE_CLOSE:
					is_open = false;
					break;

				case OS_EVENT_TYPE_KEY_RELEASE:
					if (event.as.key.key_code == KEY_CODE_ESCAPE)
						is_open = false;
				default:
					break;
			}
		}

		vkWaitForFences(context->logical_device, 1, &context->in_flight_fences[context->current_frame], VK_TRUE, UINT64_MAX);
		vkResetFences(context->logical_device, 1, &context->in_flight_fences[context->current_frame]);

		VkCommandBuffer command_buffer = context->command_buffers[context->current_frame];

		vkResetDescriptorPool(context->logical_device, context->frame_descriptor_pools[context->current_frame], 0);

		vkResetCommandBuffer(command_buffer, 0);

		VkCommandBufferBeginInfo cb_begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};

		if (vkBeginCommandBuffer(command_buffer, &cb_begin_info) != VK_SUCCESS) {
			LOG_ERROR("failed to begin command buffer recording.");
			break;
		}

		VkImageMemoryBarrier image_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
			.oldLayout = 0,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image.handle,
			.subresourceRange = image.view_info.subresourceRange,
		};

		vkCmdPipelineBarrier(
			command_buffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0,
			0, NULL,
			0, NULL,
			1, &image_barrier);

		double time = (os_time_ns() - start_time) * 1e-9;
		float flash = fabsf(sinf(time));
		VkClearColorValue clear_color = {
			.float32 = { 1.0f, 0.0f, 0.0f, 1.0f },
		};
		VkImageSubresourceRange range = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.levelCount = 1,
			.layerCount = 1,
		};
		vkCmdClearColorImage(command_buffer, image.handle, VK_IMAGE_LAYOUT_GENERAL, &clear_color, 1, &range);

		image_barrier = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image.handle,
			.subresourceRange = image.view_info.subresourceRange,
		};
		vkCmdPipelineBarrier(
			command_buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, NULL,
			0, NULL,
			1, &image_barrier);

		VkBufferImageCopy copy_region = {
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource = {
			  .aspectMask = image.view_info.subresourceRange.aspectMask,
			  .mipLevel = 0,
			  .baseArrayLayer = 0,
			  .layerCount = 1,
			},
			.imageOffset = { 0 },
			.imageExtent = image.image_info.extent,
		};

		vkCmdCopyImageToBuffer(command_buffer, image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, output_buffer.handle, 1, &copy_region);

		if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
			LOG_INFO("failed to record command buffer.");
			break;
		}

		VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &command_buffer,
		};

		if (vkQueueSubmit(context->graphics_queue, 1, &submit_info, context->in_flight_fences[context->current_frame]) != VK_SUCCESS) {
			LOG_ERROR("failed to submit command buffer to queue.");
			break;
		}

		context->current_frame = (context->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	vkDeviceWaitIdle(context->logical_device);

	vkFreeMemory(context->logical_device, image.memory, NULL);
	vkFreeMemory(context->logical_device, output_buffer.memory, NULL);

	vkDestroyImageView(context->logical_device, image.view, NULL);
	vkDestroyImage(context->logical_device, image.handle, NULL);
	vkDestroyBuffer(context->logical_device, output_buffer.handle, NULL);

	gfx_shutdown(context);

	arena_scratch_end(scratch);
	os_surface_close(window);
	os_display_shutdown();
	return 0;
}

VkImageUsageFlags gfx__to_vk_image_usage(PixelFormat format, ImageUsageFlags usage) {
	VkImageUsageFlags result = 0;
	if (FLAG_GET(usage, IMAGE_USAGE_RENDER)) {
		if (format == PIXEL_FORMAT_D24_UNORM_S8_UINT || format == PIXEL_FORMAT_D32_FLOAT)
			result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		else
			result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	if (FLAG_GET(usage, IMAGE_USAGE_SAMPLE))
		result |= VK_IMAGE_USAGE_SAMPLED_BIT;

	if (FLAG_GET(usage, IMAGE_USAGE_STORAGE))
		result |= VK_IMAGE_USAGE_STORAGE_BIT;

	if (FLAG_GET(usage, IMAGE_USAGE_TRANSFER))
		result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	return result;
}

VkBufferUsageFlags gfx__to_vk_buffer_usage(BufferUsage usage) {
	VkBufferUsageFlags result = 0;
	if (FLAG_GET(usage, BUFFER_USAGE_UNIFORM))
		result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	if (FLAG_GET(usage, BUFFER_USAGE_STORAGE))
		result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	if (FLAG_GET(usage, BUFFER_USAGE_VERTEX))
		result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	if (FLAG_GET(usage, BUFFER_USAGE_INDEX))
		result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	if (FLAG_GET(usage, BUFFER_USAGE_TRANSFER))
		result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	return result;
}

VkFormat gfx__pixel_format_to_vk_format(PixelFormat format) {
	switch (format) {
		case PIXEL_FORMAT_RGBA8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case PIXEL_FORMAT_RGBA8_SRGB:
			return VK_FORMAT_R8G8B8A8_SRGB;
		case PIXEL_FORMAT_RGBA16_FLOAT:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case PIXEL_FORMAT_R32_FLOAT:
			return VK_FORMAT_R32_SFLOAT;
		case PIXEL_FORMAT_D32_FLOAT:
			return VK_FORMAT_D32_SFLOAT;
		case PIXEL_FORMAT_D24_UNORM_S8_UINT:
			return VK_FORMAT_D24_UNORM_S8_UINT;
	}

	return VK_FORMAT_UNDEFINED;
}

VkImageAspectFlags gfx__pixel_format_to_aspect(PixelFormat format) {
	if (format == PIXEL_FORMAT_D24_UNORM_S8_UINT || format == PIXEL_FORMAT_D32_FLOAT)
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

	return VK_IMAGE_ASPECT_COLOR_BIT;
}

uint32_t gfx__find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

	for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
		if ((type_filter & (1 << index)) && (memory_properties.memoryTypes[index].propertyFlags & properties) == properties) {
			return index;
		}
	}

	LOG_ERROR("Failed to find suitable memory type!");
	ASSERT(false);
	return 0;
}

GFX_Image vulkan_image_make(GFX_Context *context, uint32_t width, uint32_t height, PixelFormat format, ImageUsageFlags usage) {
	GFX_Image result = { 0 };
	LOG_DEBUG("creating vulkan image.");

	bool ok = true;
	VkImageUsageFlags vk_usage = gfx__to_vk_image_usage(format, usage);
	VkFormat vk_format = gfx__pixel_format_to_vk_format(format);
	VkImageAspectFlags aspect = gfx__pixel_format_to_aspect(format);
	result.width = width, result.height = height;

	if (ok) { // make vulkan image handle
		result.image_info = (VkImageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.flags = 0,
			.format = vk_format,
			.extent = {
			  .width = width,
			  .height = height,
			  .depth = 1,
			},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = 1,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = vk_usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
		};

		ok = vkCreateImage(context->logical_device, &result.image_info, NULL, &result.handle) == VK_SUCCESS;
	}

	if (ok) { // allocate memory
		VkMemoryRequirements memory_requirements;
		vkGetImageMemoryRequirements(context->logical_device, result.handle, &memory_requirements);

		VkMemoryAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memory_requirements.size,
			.memoryTypeIndex = gfx__find_memory_type(context->physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
		};

		ok = vkAllocateMemory(context->logical_device, &allocate_info, NULL, &result.memory) == VK_SUCCESS;
	}

	if (ok) // bind memory to handle
		vkBindImageMemory(context->logical_device, result.handle, result.memory, 0);

	if (ok) { // create image view
		result.view_info = (VkImageViewCreateInfo){
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = result.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = result.image_info.format,
			.components = {
			  .r = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .g = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .b = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
			  .aspectMask = aspect,
			  .baseMipLevel = 0,
			  .levelCount = 1,
			  .baseArrayLayer = 0,
			  .layerCount = 1,
			}
		};

		ok = vkCreateImageView(context->logical_device, &result.view_info, NULL, &result.view) == VK_SUCCESS;
	}

	/* LOG_INFO("image loaded successfuly (%ux%u | %s)", indexof(context->image_pool, image), width, height, image_format_to_string[format]); */
	return result;
}

GFX_Buffer vulkan_buffer_make(GFX_Context *context, uint64_t size, BufferMemory memory, BufferUsage usage) {
	GFX_Buffer result = { 0 };
	LOG_DEBUG("creating vulkan buffer.");

	bool ok = true;
	VkBufferUsageFlags vk_usage = gfx__to_vk_buffer_usage(usage);
	VkMemoryPropertyFlags memory_flags = memory == BUFFER_MEMORY_LOCAl
		? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		: VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if (ok) { // create buffer handle
		result.size = size;

		uint32_t family_indices[] = { context->graphics_index };

		result.info = (VkBufferCreateInfo){
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = result.size,
			.usage = vk_usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 1,
			.pQueueFamilyIndices = family_indices
		};

		ok = vkCreateBuffer(context->logical_device, &result.info, NULL, &result.handle) == VK_SUCCESS;
	}

	if (ok) { // allocate buffer memory
		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(context->logical_device, result.handle, &memory_requirements);

		uint32_t memory_type_index = gfx__find_memory_type(context->physical_device, memory_requirements.memoryTypeBits, memory_flags);
		size_t allocation_size = memory_requirements.size;

		VkMemoryAllocateInfo allocate_info = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memory_requirements.size,
			.memoryTypeIndex = memory_type_index,
		};

		ok = vkAllocateMemory(context->logical_device, &allocate_info, NULL, &result.memory) == VK_SUCCESS;
	}

	if (ok) // bind memory
		ok = vkBindBufferMemory(context->logical_device, result.handle, result.memory, 0) == VK_SUCCESS;

	return result;
}

static inline void gfx__load_debug_extensions(GFX_Context *device);
static inline VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	void *pUserData);

bool gfx__validate_extensions(const char *required[], uint32_t required_count, VkExtensionProperties *available, uint32_t available_count) {
	bool result = true;

	for (uint32_t required_index = 0; required_index < required_count; ++required_index) {
		bool found = false;
		for (uint32_t available_index = 0; available_index < available_count; ++available_index) {
			if (strcmp(available[available_index].extensionName, required[required_index]) == 0) {
				found = true;
				break;
			}
		}
		if (found == false) {
			LOG_ERROR("required extension '%s' not found, aborting", required[required_index]);
			result = false;
			break;
		}
	}

	return result;
}

VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info = {
	.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
	.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
	.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
	.pfnUserCallback = vulkan_debug_callback,
	.pUserData = 0
};

// EXTENSIONS
static VKAPI_ATTR VkResult VKAPI_CALL STUB_vkCreateDebugUtilsMessenger(VkInstance i, const VkDebugUtilsMessengerCreateInfoEXT *c, const VkAllocationCallbacks *a, VkDebugUtilsMessengerEXT *m) { return VK_ERROR_EXTENSION_NOT_PRESENT; }
static VKAPI_ATTR void VKAPI_CALL STUB_vkDestroyDebugUtilsMessenger(VkInstance i, VkDebugUtilsMessengerEXT m, const VkAllocationCallbacks *a) {}
static VKAPI_ATTR VkResult VKAPI_CALL STUB_vkSetDebugUtilsObjectName(VkDevice d, const VkDebugUtilsObjectNameInfoEXT *n) { return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL STUB_vkCmdBeginDebugUtilsLabel(VkCommandBuffer c, const VkDebugUtilsLabelEXT *l) {}
static VKAPI_ATTR void VKAPI_CALL STUB_vkCmdEndDebugUtilsLabel(VkCommandBuffer c) {}

PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessenger = STUB_vkCreateDebugUtilsMessenger;
PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessenger = STUB_vkDestroyDebugUtilsMessenger;
PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = STUB_vkSetDebugUtilsObjectName;
PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabel = STUB_vkCmdBeginDebugUtilsLabel;
PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabel = STUB_vkCmdEndDebugUtilsLabel;

bool gfx__vk_instance_make(GFX_Context *context);
bool gfx__vk_device_make(GFX_Context *context);
bool gfx__vk_command_buffers_make(GFX_Context *context);
bool gfx__vk_synchronization_objects_make(GFX_Context *context);
bool gfx__vk_descriptor_pool(GFX_Context *context);

bool gfx_startup(GFX_Context *context) {
	bool ok = true;

	if (ok)
		ok = gfx__vk_instance_make(context);

	if (ok)
		ok = gfx__vk_device_make(context);

	if (ok)
		ok = gfx__vk_command_buffers_make(context);

	if (ok)
		ok = gfx__vk_synchronization_objects_make(context);

	if (ok)
		ok = gfx__vk_descriptor_pool(context);

	if (ok)
		context->global_range = (VkPushConstantRange){
			.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
			.offset = 0,
			.size = 128
		};

	return ok;
}

void gfx_shutdown(GFX_Context *context) {
#ifndef BUILD_DIST
	if (context->debug_messenger)
		vkDestroyDebugUtilsMessenger(context->instance, context->debug_messenger, NULL);
#endif

	for (uint32_t index = 0; index < countof(context->frame_descriptor_pools); ++index) {
		if (context->frame_descriptor_pools[index])
			vkDestroyDescriptorPool(context->logical_device, context->frame_descriptor_pools[index], NULL);
	}

	/* for (uint32_t index = 0; index < countof(context->image_available_semaphores); ++index) { */
	/* 	if (context->image_available_semaphores[index]) */
	/* 		vkDestroySemaphore(context->logical_device, context->image_available_semaphores[index], NULL); */
	/* } */

	/* for (uint32_t index = 0; index < countof(context->render_finished_semaphores); ++index) { */
	/* 	if (context->render_finished_semaphores[index]) */
	/* 		vkDestroySemaphore(context->logical_device, context->render_finished_semaphores[index], NULL); */
	/* } */

	for (uint32_t index = 0; index < countof(context->in_flight_fences); ++index) {
		if (context->in_flight_fences[index])
			vkDestroyFence(context->logical_device, context->in_flight_fences[index], NULL);
	}

	if (context->graphics_command_pool)
		vkDestroyCommandPool(context->logical_device, context->graphics_command_pool, NULL);
	if (context->logical_device)
		vkDestroyDevice(context->logical_device, NULL);
	if (context->instance)
		vkDestroyInstance(context->instance, NULL);

	memory_zero(context, sizeof(GFX_Context));
}

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
	VkDebugUtilsMessageTypeFlagsEXT message_type,
	const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
	void *pUserData) {
	switch (message_severity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: {
			LOG_TRACE("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: {
			LOG_INFO("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
			LOG_WARN("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
			LOG_ERROR("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		default: {
			return VK_FALSE;
		} break;
	}
}

void gfx__load_debug_extensions(GFX_Context *device) {
#define LOAD_EXTENSION(instance, name)                                    \
	name = (PFN_##name##EXT)vkGetInstanceProcAddr(instance, #name "EXT"); \
	if (!name) {                                                          \
		LOG_ERROR("Failed to load extension: " #name "EXT");              \
		name = STUB_##name;                                               \
	}

	LOAD_EXTENSION(device->instance, vkCreateDebugUtilsMessenger);
	LOAD_EXTENSION(device->instance, vkDestroyDebugUtilsMessenger);
	LOAD_EXTENSION(device->instance, vkSetDebugUtilsObjectName);
	LOAD_EXTENSION(device->instance, vkCmdBeginDebugUtilsLabel);
	LOAD_EXTENSION(device->instance, vkCmdEndDebugUtilsLabel);

#undef LOAD_EXTENSION
}

bool gfx__vk_instance_make(GFX_Context *context) {
	LOG_DEBUG("initializing vulkan instance.");
	ArenaTemp scratch = arena_scratch_begin(0);

	uint32_t required_extension_count = 0;
	const char **required_extensions = os_surface_vulkan_extensions(&required_extension_count);

	bool ok = true;
	if (ok) { // validate if required extensions are present
#ifndef BUILD_DIST
		const char **debug_extensions = arena_push_count(scratch.arena, const char **, required_extension_count + 1);
		memory_copy(debug_extensions, required_extensions, sizeof(*required_extensions) * required_extension_count);
		debug_extensions[required_extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		required_extensions = debug_extensions;
#endif

		uint32_t available_extension_count = 0;
		vkEnumerateInstanceExtensionProperties(0, &available_extension_count, 0);

		VkExtensionProperties *available_extensions = arena_push_count(scratch.arena, VkExtensionProperties, available_extension_count);
		vkEnumerateInstanceExtensionProperties(0, &available_extension_count, available_extensions);
		ok = gfx__validate_extensions(required_extensions, required_extension_count, available_extensions, available_extension_count);
	}
	static const char *requested_layers[] = {
#ifndef BUILD_DIST
		"VK_LAYER_KHRONOS_validation",
#endif
	};
	uint32_t requested_layer_count = countof(requested_layers);

	if (ok) { // validate if required layers are present
		uint32_t available_layer_count;
		vkEnumerateInstanceLayerProperties(&available_layer_count, 0);

		VkLayerProperties *available_layers = arena_push_count(scratch.arena, VkLayerProperties, available_layer_count);
		vkEnumerateInstanceLayerProperties(&available_layer_count, available_layers);

		for (uint32_t request_index = 0; request_index < requested_layer_count; ++request_index) {
			bool found = false;
			for (uint32_t layer_index = 0; layer_index < available_layer_count; ++layer_index) {
				if (strcmp(available_layers[layer_index].layerName, requested_layers[request_index]) == 0) {
					found = true;
					break;
				}
			}

			if (found == false) {
				LOG_ERROR("layer '%s' not found, aborting", requested_layers[request_index]);
				ok = false;
			}
		}
	}

	if (ok) { // create vulkan instance
		VkApplicationInfo app_info = {
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "unnamed",
			.applicationVersion = 1,
			.pEngineName = "unnamed",
			.engineVersion = 1,
			.apiVersion = VK_MAKE_VERSION(1, 3, 0)
		};

		VkInstanceCreateInfo instance_info = {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &app_info,
			.enabledExtensionCount = required_extension_count,
			.ppEnabledExtensionNames = required_extensions,
			.ppEnabledLayerNames = requested_layers,
			.enabledLayerCount = requested_layer_count,

#ifndef BUILD_DIST
			.pNext = &debug_utils_create_info,
#endif
		};

		ok = vkCreateInstance(&instance_info, 0, &context->instance) == VK_SUCCESS;
	}

	if (ok) { // load debug extension pointers & craete debug util
#ifndef BUILD_DIST
		gfx__load_debug_extensions(context);
		vkCreateDebugUtilsMessenger(context->instance, &debug_utils_create_info, 0, &context->debug_messenger);
#endif
	}

	arena_scratch_end(scratch);
	return ok;
}

const char *required_device_extensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

bool gfx__device_suitable(GFX_Context *context, VkPhysicalDevice device) {
	ArenaTemp scratch = arena_scratch_begin(NULL);

	bool ok = true;
	if (ok) { // check if vulkan 1.3 is supported
		VkPhysicalDeviceProperties physical_device_properties = { 0 };
		vkGetPhysicalDeviceProperties(device, &physical_device_properties);

		ok = physical_device_properties.apiVersion >= VK_API_VERSION_1_3;
	}

	/* if (ok) { // check for present support  */
	/* 	uint32_t queue_family_count = 0; */
	/* 	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, 0); */

	/* 	VkQueueFamilyProperties *queue_family_properties = arena_push_count(scratch.arena, VkQueueFamilyProperties, queue_family_count); */
	/* 	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_family_properties); */

	/* 	VkBool32 supports_present = VK_FALSE; */
	/* 	for (uint32_t queue_family_index = 0; queue_family_index < queue_family_count; ++queue_family_index) { */
	/* 		vkGetPhysicalDeviceSurfaceSupportKHR(device, queue_family_index, context->surface.handle, &supports_present); */

	/* 		if ((queue_family_properties[queue_family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) && supports_present) */
	/* 			break; */
	/* 	} */

	/* 	if (supports_present == false) */
	/* 		ok = false; */
	/* } */

	if (ok) { // validate if required device extensions are present
		uint32_t available_extension_count = 0;
		vkEnumerateDeviceExtensionProperties(device, NULL, &available_extension_count, NULL);

		VkExtensionProperties *available_extensions = arena_push_count(scratch.arena, VkExtensionProperties, available_extension_count);
		vkEnumerateDeviceExtensionProperties(device, NULL, &available_extension_count, available_extensions);

		ok = gfx__validate_extensions(required_device_extensions, countof(required_device_extensions), available_extensions, available_extension_count);
	}

	if (ok) { // validate if desired features are present
		VkPhysicalDeviceVulkan13Features vk13_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.dynamicRendering = VK_TRUE,
		};
		VkPhysicalDeviceFeatures2 vk_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &vk13_features,
		};
		vkGetPhysicalDeviceFeatures2(device, &vk_features);

		if (vk13_features.dynamicRendering == false)
			ok = false;
	}

	arena_scratch_end(scratch);
	return ok;
}

bool gfx__vk_device_make(GFX_Context *context) {
	LOG_DEBUG("initializing vulkan device.");
	ArenaTemp scratch = arena_scratch_begin(0);

	bool ok = true;
	if (ok) { // select physical device

		uint32_t physical_device_count = 0;
		vkEnumeratePhysicalDevices(context->instance, &physical_device_count, 0);

		VkPhysicalDevice *physical_devices = arena_push_count(scratch.arena, VkPhysicalDevice, physical_device_count);
		vkEnumeratePhysicalDevices(context->instance, &physical_device_count, physical_devices);

		for (uint32_t physical_device_index = 0; physical_device_index < physical_device_count; ++physical_device_index) {
			if (gfx__device_suitable(context, physical_devices[physical_device_index])) {
				context->physical_device = physical_devices[physical_device_index];
				break;
			}
		}

		if (context->physical_device == 0) {
			LOG_ERROR("failed to find suitable graphics card with Vulkan 1.3 support.");
			ok = false;
		}
	}

	if (ok) { // find queue indices
		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device, &queue_family_count, NULL);

		VkQueueFamilyProperties *queue_family_properties = arena_push_count(scratch.arena, VkQueueFamilyProperties, queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(context->physical_device, &queue_family_count, queue_family_properties);

		context->graphics_index = -1, context->present_index = -1,
		context->transfer_index = -1, context->compute_index = -1;

		for (uint32_t index = 0; index < queue_family_count; ++index) {
			VkQueueFlags flags = queue_family_properties[index].queueFlags;

			/* VkBool32 present_support = false; */
			/* vkGetPhysicalDeviceSurfaceSupportKHR(context->physical_device, index, context->surface.handle, &present_support); */

			if ((flags & VK_QUEUE_GRAPHICS_BIT) && context->graphics_index == -1) {
				context->graphics_index = index;
				context->present_index = index;
				/* ASSERT(present_support && "grahpics index does not support presenting"); */
			}

			/* if (present_support && context->present_index == -1) */
			/* 	context->present_index = index; */

			if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_TRANSFER_BIT) && context->transfer_index == -1) // dedicated transfer
				context->transfer_index = index;

			if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_COMPUTE_BIT) && context->compute_index == -1) // dedicated compute
				context->compute_index = index;
		}

		if (context->graphics_index == -1 || context->present_index == -1) {
			LOG_ERROR("failed to find graphics queues");
			ok = false;
		}
	}

	if (ok) { // create vulkan device
		float queue_priortiy = 0.5f;

		uint32_t queue_family_indices[] = { context->graphics_index }; // TODO: Multiple queues
		VkDeviceQueueCreateInfo queue_infos[] = {
			{
			  .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			  .queueFamilyIndex = context->graphics_index,
			  .queueCount = 1,
			  .pQueuePriorities = &queue_priortiy,
			}
		};

		VkPhysicalDeviceVulkan13Features vk13_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
			.dynamicRendering = VK_TRUE,
		};
		VkPhysicalDeviceVulkan12Features vk12_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
			.pNext = &vk13_features,
			.runtimeDescriptorArray = VK_TRUE,
			.descriptorBindingPartiallyBound = VK_TRUE,
			.shaderSampledImageArrayNonUniformIndexing = VK_TRUE, // NOTE: Works perfectly fine without
			.descriptorIndexing = VK_TRUE,
		};
		VkPhysicalDeviceFeatures2 enable_features = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &vk12_features,
			.features = { .samplerAnisotropy = true, .fillModeNonSolid = true },
		};

		VkDeviceCreateInfo device_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext = &enable_features,
			.queueCreateInfoCount = countof(queue_infos),
			.pQueueCreateInfos = queue_infos,
			.enabledExtensionCount = countof(required_device_extensions),
			.ppEnabledExtensionNames = required_device_extensions,
		};

		ok = vkCreateDevice(context->physical_device, &device_info, NULL, &context->logical_device) == VK_SUCCESS;
	}

	if (ok) { // get the queue handles from indices
		vkGetDeviceQueue(context->logical_device, context->graphics_index, 0, &context->graphics_queue);
		vkGetDeviceQueue(context->logical_device, context->present_index, 0, &context->present_queue); // NOTE: exact same handle as grahpics
	}

	arena_scratch_end(scratch);
	return ok;
}

bool gfx__vk_command_buffers_make(GFX_Context *context) {
	LOG_DEBUG("initializing vulkan command buffers.");

	bool ok = true;
	if (ok) { // make graphics command pool
		VkCommandPoolCreateInfo cp_create_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = context->graphics_index
		};
		ok = vkCreateCommandPool(context->logical_device, &cp_create_info, NULL, &context->graphics_command_pool) == VK_SUCCESS;
	}

	/* if (ok) { // make transfer command pool */
	/* 	VkCommandPoolCreateInfo cp_create_info = { */
	/* 		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, */
	/* 		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, */
	/* 		.queueFamilyIndex = context->graphics_index */
	/* 	}; */

	/* 	ok = vkCreateCommandPool(context->logical_device, &cp_create_info, NULL, &context->transfer_command_pool) == VK_SUCCESS; */
	/* } */

	if (ok) { // allocate command buffers
		VkCommandBufferAllocateInfo cb_allocate_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = context->graphics_command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = countof(context->command_buffers)
		};
		ok = vkAllocateCommandBuffers(context->logical_device, &cb_allocate_info, context->command_buffers) == VK_SUCCESS;
	}

	return ok;
}

bool gfx__vk_synchronization_objects_make(GFX_Context *context) {
	LOG_DEBUG("initializing vulkan synchronization objects.");

	bool ok = true;
	if (ok) { // make synchronization objects
		VkSemaphoreCreateInfo s_create_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};

		VkFenceCreateInfo f_create_info = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT
		};

		// create semaphores
		/* for (uint32_t index = 0; index < countof(context->image_available_semaphores); ++index) */
		/* 	ok &= vkCreateSemaphore(context->logical_device, &s_create_info, NULL, context->image_available_semaphores + index) == VK_SUCCESS; */
		/* for (uint32_t index = 0; index < countof(context->render_finished_semaphores); ++index) */
		/* 	ok &= vkCreateSemaphore(context->logical_device, &s_create_info, NULL, context->render_finished_semaphores + index) == VK_SUCCESS; */

		// create fences
		for (uint32_t index = 0; index < countof(context->in_flight_fences); ++index)
			ok &= vkCreateFence(context->logical_device, &f_create_info, NULL, context->in_flight_fences + index) == VK_SUCCESS;
	}

	return ok;
}

bool gfx__vk_descriptor_pool(GFX_Context *context) {
	LOG_DEBUG("initializing global vulkan descriptor pools.");

	bool ok = true;
	if (ok) { // create global descriptor pool per frame in flight
		VkDescriptorPoolSize sizes[] = {
			{
			  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
			  .descriptorCount = 1000,
			},
			{
			  .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  .descriptorCount = 1000,
			},
		};

		VkDescriptorPoolCreateInfo dp_create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.poolSizeCount = countof(sizes),
			.pPoolSizes = sizes,
			.maxSets = 1000,
		};

		for (uint32_t frame_index = 0; frame_index < MAX_FRAMES_IN_FLIGHT; ++frame_index)
			ok &= vkCreateDescriptorPool(context->logical_device, &dp_create_info, NULL, &context->frame_descriptor_pools[frame_index]) == VK_SUCCESS;
	}

	return ok;
}
