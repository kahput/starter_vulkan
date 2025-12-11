#include "renderer/vk_renderer.h"

#include "renderer/vulkan/vk_types.h"
#include "vk_internal.h"

#include "core/logger.h"
#include <assert.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

// static bool create_buffer(VulkanContext *, uint32_t, VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags, VkBuffer *, VkDeviceMemory *);

int32_t to_vulkan_usage(BufferType type);

bool vulkan_renderer_create_buffer(VulkanContext *context, uint32_t store_index, BufferType type, size_t size, void *data) {
	const char *stringify[] = {
		"Vertex buffer",
		"Index buffer",
		"Uniform buffer",
	};

	if (store_index >= MAX_BUFFERS) {
		LOG_ERROR("Vulkan: Buffer index %d out of bounds", store_index);
		return false;
	}

	VulkanBuffer *buffer = &context->buffer_pool[store_index];
	if (buffer->handle[0] != NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to allocate buffer at index %d, but index is already in use", store_index);
		assert(false);
		return false;
	}

	LOG_INFO("Vulkan: Creating %s resource...", stringify[type]);

	logger_indent();

	buffer->count = 1;
	buffer->size = size;

	if (type == BUFFER_TYPE_UNIFORM) {
		buffer->count = MAX_FRAMES_IN_FLIGHT;
		bool result = vulkan_create_uniform_buffers(context, buffer, buffer->size, data);
		logger_dedent();
		return result;
	}

	if (data == NULL) {
		bool result = vulkan_create_buffer(context, context->device.graphics_index, buffer->size, buffer->usage, buffer->memory_property_flags, &buffer->handle[0], &buffer->memory[0]);
		logger_dedent();
		return result;
	}

	buffer->usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | to_vulkan_usage(type);
	buffer->memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;

	LOG_DEBUG("Creating staging buffer...");
	logger_indent();
	vulkan_create_buffer(context, context->device.graphics_index, buffer->size, staging_usage, staging_properties, &staging_buffer, &staging_buffer_memory);
	logger_dedent();

	vkMapMemory(context->device.logical, staging_buffer_memory, 0, buffer->size, 0, &buffer->mapped[0]);
	memcpy(buffer->mapped[0], data, buffer->size);
	vkUnmapMemory(context->device.logical, staging_buffer_memory);

	LOG_DEBUG("Creating buffer...");
	logger_indent();
	vulkan_create_buffer(context, context->device.graphics_index, buffer->size, buffer->usage, buffer->memory_property_flags, &buffer->handle[0], &buffer->memory[0]);
	logger_dedent();

	vulkan_copy_buffer(context, staging_buffer, buffer->handle[0], buffer->size);
	vkDestroyBuffer(context->device.logical, staging_buffer, NULL);
	vkFreeMemory(context->device.logical, staging_buffer_memory, NULL);

	LOG_INFO("%s resource created", stringify[type]);

	logger_dedent();

	return true;
}

bool vulkan_renderer_destroy_buffer(VulkanContext *context, uint32_t retrieve_index) {
	if (retrieve_index >= MAX_BUFFERS) {
		LOG_ERROR("Vulkan: Buffer index %d out of bounds", retrieve_index);
		return false;
	}

	VulkanBuffer *buffer = &context->buffer_pool[retrieve_index];
	if (buffer->handle[0] == NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to destroy buffer at index %d, but index is already in use", retrieve_index);
		assert(false);
		return false;
	}

	for (uint32_t index = 0; index < MAX_FRAMES_IN_FLIGHT; ++index) {
		if (buffer->handle[index] != NULL) {
			vkDestroyBuffer(context->device.logical, buffer->handle[index], NULL);
			vkFreeMemory(context->device.logical, buffer->memory[index], NULL);

			buffer->handle[index] = NULL;
			buffer->memory[index] = NULL;
		}
	}

	return true;
}

bool vulkan_renderer_update_buffer(VulkanContext *context, uint32_t retrieve_index, uint32_t offset, size_t size, void *data) {
	const VulkanBuffer *buffer = &context->buffer_pool[retrieve_index];
	if (buffer->handle[0] == NULL) {
		LOG_FATAL("Vulkan: Renderer requested to update buffer at index %d, but no valid buffer found at index", retrieve_index);
		assert(false);
		return false;
	}

	size_t copy_size = size;
	if (buffer->size < copy_size) {
		LOG_WARN("Update size bigger than allocation size, ignoring difference of %dB", size - buffer->size);
		copy_size = buffer->size;
	}

	if ((buffer->usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) == VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
		uint8_t *dest = (uint8_t *)buffer->mapped[context->current_frame] + offset;
		memcpy(dest, data, copy_size);
		return true;
	}

	uint8_t *dest = (uint8_t *)buffer->mapped[0] + offset;
	memcpy(dest, data, copy_size);
	return true;
}

bool vulkan_renderer_bind_buffer(VulkanContext *context, uint32_t retrieve_index) {
	const VulkanBuffer *buffer = &context->buffer_pool[retrieve_index];
	if (buffer->handle[0] == NULL) {
		LOG_FATAL("Vulkan: Renderer requested to bind buffer at index %d, but no valid buffer found at index", retrieve_index);
		assert(false);
		return false;
	}

	if (buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
		VkBuffer vertex_buffer[1] = { buffer->handle[0] };
		VkDeviceSize offset[1] = { 0 };

		vkCmdBindVertexBuffers(context->command_buffers[context->current_frame], 0, 1, vertex_buffer, offset);
		return true;
	}

	if (buffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
		vkCmdBindIndexBuffer(context->command_buffers[context->current_frame], buffer->handle[0], 0, VK_INDEX_TYPE_UINT32);
		return true;
	}

	LOG_WARN("Buffer failed to bind: Unsupported buffer type");

	return false;
}

bool vulkan_renderer_bind_buffers(VulkanContext *context, uint32_t *buffers, uint32_t count) {
	if (buffers == NULL || count <= 0) {
		LOG_ERROR("Vulkan: Invalid arguments passed, aborting bind");
		return false;
	}

	const VulkanBuffer *buffer = &context->buffer_pool[buffers[0]];
	if (buffer->handle[0] == NULL) {
		LOG_FATAL("Vulkan: Renderer requested to bind buffer at index %d, but no valid buffer found at index", buffers[0]);
		assert(false);
		return false;
	}

	if (buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
		VkBuffer vertex_buffers[16] = { 0 };
		VkDeviceSize offsets[16] = { 0 };
		for (uint32_t index = 0; index < count; ++index) {
			vertex_buffers[index] = context->buffer_pool[buffers[index]].handle[0];
			offsets[index] = 0;
		}

		vkCmdBindVertexBuffers(context->command_buffers[context->current_frame], 0, count, vertex_buffers, offsets);
		return true;
	}

	if (buffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
		vkCmdBindIndexBuffer(context->command_buffers[context->current_frame], buffer->handle[0], 0, VK_INDEX_TYPE_UINT32);
		return true;
	}

	LOG_WARN("Buffer failed to bind: Unsupported buffer type");

	return false;
}

bool vulkan_create_uniform_buffers(VulkanContext *context, VulkanBuffer *buffer, size_t size, void *data) {
	buffer->usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	buffer->memory_property_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	for (uint32_t index = 0; index < MAX_FRAMES_IN_FLIGHT; ++index) {
		vulkan_create_buffer(context, context->device.graphics_index, size, buffer->usage, buffer->memory_property_flags, &buffer->handle[index], &buffer->memory[index]);

		vkMapMemory(context->device.logical, buffer->memory[index], 0, size, 0, &buffer->mapped[index]);

		if (data) {
			memcpy(buffer->mapped[index], data, size);
		}
	}

	return true;
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
		// .queueFamilyIndexCount = countof(family_indices),
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

	LOG_DEBUG("VkBuffer created");

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
