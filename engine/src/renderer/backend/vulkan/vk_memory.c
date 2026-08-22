#include "core/debug.h"
#include "core/logger.h"
#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"
#include <vulkan/vulkan_core.h>

VkDeviceSize vulkan_memory_required_alignment(VulkanContext *context, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties) {
	// NOTE: Seems like device local index/vertex buffers don't need alignment at all
	VkDeviceSize alignment = 1;

	if (FLAG_GET(memory_properties, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
		alignment = context->device.properties.limits.minMemoryMapAlignment;
	}

	if (FLAG_GET(usage, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
		VkDeviceSize ubo_align = context->device.properties.limits.minUniformBufferOffsetAlignment;
		alignment = MAX(alignment, ubo_align);
	}

	if (FLAG_GET(usage, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
		VkDeviceSize ssbo_align = context->device.properties.limits.minStorageBufferOffsetAlignment;
		alignment = MAX(alignment, ssbo_align);
	}

	return alignment;
}

uint32_t vulkan_memory_type_find(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
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
