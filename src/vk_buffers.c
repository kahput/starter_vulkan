#include "core/arena.h"
#include "vk_renderer.h"

#include "core/logger.h"
#include <string.h>
#include <vulkan/vulkan_core.h>

bool vk_create_vertex_buffer(struct arena *scratch_arena, VKRenderer *renderer, Mesh *mesh) {
	VkDeviceSize size = mesh->primitves->vertex_count * sizeof(*mesh->primitves->vertices);
	LOG_INFO("Creating VkBuffer of size %d (count = %d)", size, mesh->primitves->vertex_count);

	VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;

	vk_create_buffer(renderer, size, staging_usage, staging_properties, &staging_buffer, &staging_buffer_memory);

	void *data;
	vkMapMemory(renderer->logical_device, staging_buffer_memory, 0, size, 0, &data);
	memcpy(data, mesh->primitves->vertices, size);
	vkUnmapMemory(renderer->logical_device, staging_buffer_memory);

	vk_create_buffer(renderer, size, usage, properties, &renderer->vertex_buffer, &renderer->vertex_buffer_memory);

	vk_copy_buffer(renderer, staging_buffer, renderer->vertex_buffer, size);
	vkDestroyBuffer(renderer->logical_device, staging_buffer, NULL);
	vkFreeMemory(renderer->logical_device, staging_buffer_memory, NULL);

	return true;
}

bool vk_create_index_buffer(struct arena *scratch_arena, VKRenderer *renderer, Mesh *mesh) {
	VkDeviceSize size = mesh->primitves->index_count * sizeof(*mesh->primitves->indices);

	VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;

	vk_create_buffer(renderer, size, staging_usage, staging_properties, &staging_buffer, &staging_buffer_memory);

	void *data;
	vkMapMemory(renderer->logical_device, staging_buffer_memory, 0, size, 0, &data);
	memcpy(data, mesh->primitves->indices, size);
	vkUnmapMemory(renderer->logical_device, staging_buffer_memory);

	vk_create_buffer(renderer, size, usage, properties, &renderer->index_buffer, &renderer->index_buffer_memory);

	vk_copy_buffer(renderer, staging_buffer, renderer->index_buffer, size);
	vkDestroyBuffer(renderer->logical_device, staging_buffer, NULL);
	vkFreeMemory(renderer->logical_device, staging_buffer_memory, NULL);

	return true;
}

bool vk_create_uniform_buffers(struct arena *scratch_arena, VKRenderer *renderer) {
	VkDeviceSize size = sizeof(MVPObject);

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		vk_create_buffer(renderer, size, usage, properties, renderer->uniform_buffers + i, renderer->uniform_buffers_memory + i);

		vkMapMemory(renderer->logical_device, renderer->uniform_buffers_memory[i], 0, size, 0, &renderer->uniform_buffers_mapped[i]);
	}

	size = sizeof(Material);
	vk_create_buffer(renderer, size, usage, properties, &renderer->material_uniform_bufffer, &renderer->material_uniform_buffer_memory);
	vkMapMemory(renderer->logical_device, renderer->material_uniform_buffer_memory, 0, size, 0, &renderer->material_uniform_buffer_mapped);

	return true;
}

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
		if (type_filter & (1 << i) && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	LOG_ERROR("Failed to find suitable memory type!");
	return 0;
}

bool vk_create_buffer(
	VKRenderer *renderer,
	VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	VkBuffer *buffer, VkDeviceMemory *buffer_memory) {
	uint32_t family_indices[] = { renderer->family_indices.graphics, renderer->family_indices.transfer };

	VkBufferCreateInfo vb_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = family_indices
		// .queueFamilyIndexCount = array_count(family_indices),
		// .pQueueFamilyIndices = family_indices
	};

	if (vkCreateBuffer(renderer->logical_device, &vb_create_info, NULL, buffer) != VK_SUCCESS) {
		LOG_ERROR("Failed to create vertex buffer");
		return false;
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(renderer->logical_device, *buffer, &memory_requirements);

	VkMemoryAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = find_memory_type(renderer->physical_device, memory_requirements.memoryTypeBits, properties),
	};

	if (vkAllocateMemory(renderer->logical_device, &allocate_info, NULL, buffer_memory) != VK_SUCCESS) {
		LOG_ERROR("Failed to allocate vertex buffer memory");
		return false;
	}

	vkBindBufferMemory(renderer->logical_device, *buffer, *buffer_memory, 0);

	LOG_INFO("VkBuffer created");

	return true;
}

bool vk_copy_buffer(VKRenderer *renderer, VkBuffer src, VkBuffer dst, VkDeviceSize size) {
	VkCommandBuffer command_buffer;
	vk_begin_single_time_commands(renderer, renderer->transfer_command_pool, &command_buffer);

	VkBufferCopy copy_region = { .size = size };
	vkCmdCopyBuffer(command_buffer, src, dst, 1, &copy_region);

	vk_end_single_time_commands(renderer, renderer->transfer_queue, renderer->transfer_command_pool, &command_buffer);
	return true;
}
