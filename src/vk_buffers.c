#include "vk_renderer.h"

#include "core/logger.h"
#include <string.h>
#include <vulkan/vulkan_core.h>

uint32_t find_memory_type(VKRenderer *renderer, uint32_t type_filter, VkMemoryPropertyFlags properties);

bool vk_create_vertex_buffer(VKRenderer *renderer) {
	VkBufferCreateInfo vb_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = sizeof(vertices),
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	if (vkCreateBuffer(renderer->logical_device, &vb_create_info, NULL, &renderer->vertex_buffer) != VK_SUCCESS) {
		LOG_ERROR("Failed to create vertex buffer");
		return false;
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(renderer->logical_device, renderer->vertex_buffer, &memory_requirements);

	VkMemoryAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = find_memory_type(renderer, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
	};

	if (vkAllocateMemory(renderer->logical_device, &allocate_info, NULL, &renderer->vertex_buffer_memory) != VK_SUCCESS) {
		LOG_ERROR("Failed to allocate vertex buffer memory");
		return false;
	}

	vkBindBufferMemory(renderer->logical_device, renderer->vertex_buffer, renderer->vertex_buffer_memory, 0);

	void *data;
	vkMapMemory(renderer->logical_device, renderer->vertex_buffer_memory, 0, vb_create_info.size, 0, &data);
	memcpy(data, vertices, sizeof(vertices));
	vkUnmapMemory(renderer->logical_device, renderer->vertex_buffer_memory);

	return true;
}

uint32_t find_memory_type(VKRenderer *renderer, uint32_t type_filter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(renderer->physical_device, &memory_properties);

	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
		if (type_filter & (1 << i) && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	LOG_ERROR("Failed to find suitable memory type!");
	return 0;
}
