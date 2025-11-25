#include "core/arena.h"
#include "renderer/vk_renderer.h"

#include "core/logger.h"
#include <string.h>
#include <vulkan/vulkan_core.h>

// static bool create_buffer(VulkanContext *, uint32_t, VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags, VkBuffer *, VkDeviceMemory *);

int32_t to_vulkan_usage(BufferType type);

Buffer *vulkan_renderer_create_buffer(Arena *arena, VulkanContext *context, BufferType type, size_t size, void *data) {
	Buffer *buffer = arena_push_type(arena, Buffer);
	VulkanBuffer *internal = arena_push_type(arena, VulkanBuffer);
	buffer->internal = internal;

	internal->usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | to_vulkan_usage(type);
	internal->memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;

	vulkan_create_buffer(context, context->device.graphics_index, size, staging_usage, staging_properties, &staging_buffer, &staging_buffer_memory);

	void *mapped_data;
	vkMapMemory(context->device.logical, staging_buffer_memory, 0, size, 0, &mapped_data);
	memcpy(mapped_data, data, size);
	vkUnmapMemory(context->device.logical, staging_buffer_memory);

	vulkan_create_buffer(context, context->device.graphics_index, size, internal->usage, internal->memory_property_flags, &internal->handle, &internal->memory);

	vulkan_copy_buffer(context, staging_buffer, internal->handle, size);
	vkDestroyBuffer(context->device.logical, staging_buffer, NULL);
	vkFreeMemory(context->device.logical, staging_buffer_memory, NULL);

	return buffer;
}

bool vulkan_create_uniform_buffers(VulkanContext *context) {
	VkDeviceSize size = sizeof(MVPObject);

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		vulkan_create_buffer(context, context->device.graphics_index, size, usage, properties, context->uniform_buffers + i, context->uniform_buffers_memory + i);

		vkMapMemory(context->device.logical, context->uniform_buffers_memory[i], 0, size, 0, &context->uniform_buffers_mapped[i]);
	}

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

bool vulkan_create_buffer(
	VulkanContext *context,
	uint32_t queue_family_index,
	VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	VkBuffer *buffer, VkDeviceMemory *buffer_memory) {
	uint32_t family_indices[] = { queue_family_index };

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

	if (vkCreateBuffer(context->device.logical, &vb_create_info, NULL, buffer) != VK_SUCCESS) {
		LOG_ERROR("Failed to create vertex buffer");
		return false;
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(context->device.logical, *buffer, &memory_requirements);

	VkMemoryAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = find_memory_type(context->device.physical, memory_requirements.memoryTypeBits, properties),
	};

	if (vkAllocateMemory(context->device.logical, &allocate_info, NULL, buffer_memory) != VK_SUCCESS) {
		LOG_ERROR("Failed to allocate vertex buffer memory");
		return false;
	}

	vkBindBufferMemory(context->device.logical, *buffer, *buffer_memory, 0);

	LOG_INFO("VkBuffer created");

	return true;
}

bool vulkan_copy_buffer(VulkanContext *context, VkBuffer src, VkBuffer dst, VkDeviceSize size) {
	VkCommandBuffer command_buffer;
	vulkan_begin_single_time_commands(context, context->transfer_command_pool, &command_buffer);

	VkBufferCopy copy_region = { .size = size };
	vkCmdCopyBuffer(command_buffer, src, dst, 1, &copy_region);

	vulkan_end_single_time_commands(context, context->device.transfer_queue, context->transfer_command_pool, &command_buffer);
	return true;
}

int32_t to_vulkan_usage(BufferType type) {
	switch (type) {
		case BUFFER_TYPE_VERTEX:
			return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		case BUFFER_TYPE_INDEX:
			return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		case BUFFER_TYPE_UNIFORM:
			return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			break;
		default:
			return -1;
	}
}

bool vulkan_renderer_bind_buffer(VulkanContext *context, Buffer *buffer) {
	VulkanBuffer *vulkan_buffer = (VulkanBuffer *)buffer->internal;

	if (vulkan_buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
		VkBuffer vertex_buffers[] = { vulkan_buffer->handle };
		VkDeviceSize offsets[] = { 0 };

		vkCmdBindVertexBuffers(context->command_buffers[context->current_frame], 0, array_count(offsets), vertex_buffers, offsets);
		return true;
	}

	if (vulkan_buffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
		vkCmdBindIndexBuffer(context->command_buffers[context->current_frame], vulkan_buffer->handle, 0, VK_INDEX_TYPE_UINT32);
		return true;
	}

	LOG_WARN("Buffer failed to bind: Unsupported buffer type");

	return false;
}
