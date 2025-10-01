#include "vk_renderer.h"

#include "core/logger.h"
#include <string.h>
#include <vulkan/vulkan_core.h>

uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

// clang-format off
const Vertex vertices[] = {
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = {  0.5f, -0.5f, -0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = { -0.5f,  0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }},

	{ .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},

	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 0.0f }} ,
	{ .position = { -0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},

	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},

	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = {  0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},

	{ .position = { -0.5f,  0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
	{ .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
	{ .position = { -0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
	{ .position = { -0.5f,  0.5f, -0.5f }, .uv = { 0.0f, 1.0f }}
};
// clang-format on

const uint16_t indices[6] = {
	0, 1, 2, 2, 3, 0
};

bool vk_create_vertex_buffer(struct arena *scratch_arena, VKRenderer *renderer) {
	QueueFamilyIndices family_indices = find_queue_families(scratch_arena, renderer);

	VkDeviceSize size = sizeof(vertices);

	VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;

	vk_create_buffer(renderer, family_indices, size, staging_usage, staging_properties, &staging_buffer, &staging_buffer_memory);

	void *data;
	vkMapMemory(renderer->logical_device, staging_buffer_memory, 0, size, 0, &data);
	memcpy(data, vertices, sizeof(vertices));
	vkUnmapMemory(renderer->logical_device, staging_buffer_memory);

	vk_create_buffer(renderer, family_indices, size, usage, properties, &renderer->vertex_buffer, &renderer->vertex_buffer_memory);

	vk_copy_buffer(renderer, staging_buffer, renderer->vertex_buffer, size);
	vkDestroyBuffer(renderer->logical_device, staging_buffer, NULL);
	vkFreeMemory(renderer->logical_device, staging_buffer_memory, NULL);

	return true;
}

bool vk_create_index_buffer(struct arena *scratch_arena, VKRenderer *renderer) {
	QueueFamilyIndices family_indices = find_queue_families(scratch_arena, renderer);

	VkDeviceSize size = sizeof(indices);

	VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;

	vk_create_buffer(renderer, family_indices, size, staging_usage, staging_properties, &staging_buffer, &staging_buffer_memory);

	void *data;
	vkMapMemory(renderer->logical_device, staging_buffer_memory, 0, size, 0, &data);
	memcpy(data, indices, sizeof(indices));
	vkUnmapMemory(renderer->logical_device, staging_buffer_memory);

	vk_create_buffer(renderer, family_indices, size, usage, properties, &renderer->index_buffer, &renderer->index_buffer_memory);

	vk_copy_buffer(renderer, staging_buffer, renderer->index_buffer, size);
	vkDestroyBuffer(renderer->logical_device, staging_buffer, NULL);
	vkFreeMemory(renderer->logical_device, staging_buffer_memory, NULL);

	return true;
}

bool vk_create_uniform_buffers(struct arena *scratch_arena, VKRenderer *renderer) {
	QueueFamilyIndices family_indices = find_queue_families(scratch_arena, renderer);

	VkDeviceSize size = sizeof(MVPObject);

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		vk_create_buffer(renderer, family_indices, size, usage, properties, renderer->uniform_buffers + i, renderer->uniform_buffers_memory + i);

		vkMapMemory(renderer->logical_device, renderer->uniform_buffers_memory[i], 0, size, 0, &renderer->uniform_buffers_mapped[i]);
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

bool vk_create_buffer(
	VKRenderer *renderer,
	QueueFamilyIndices queue_familes,
	VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	VkBuffer *buffer, VkDeviceMemory *buffer_memory) {
	uint32_t family_indices[] = { queue_familes.graphics, queue_familes.transfer };

	VkBufferCreateInfo vb_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_CONCURRENT,
		.queueFamilyIndexCount = array_count(family_indices),
		.pQueueFamilyIndices = family_indices
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
		.memoryTypeIndex = find_memory_type(renderer->physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
	};

	if (vkAllocateMemory(renderer->logical_device, &allocate_info, NULL, buffer_memory) != VK_SUCCESS) {
		LOG_ERROR("Failed to allocate vertex buffer memory");
		return false;
	}

	vkBindBufferMemory(renderer->logical_device, *buffer, *buffer_memory, 0);

	return true;
}

bool vk_copy_buffer(VKRenderer *renderer, VkBuffer src, VkBuffer dst, VkDeviceSize size) {
	VkCommandBufferAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool = renderer->transfer_command_pool,
		.commandBufferCount = 1
	};

	VkCommandBuffer command_buffer;
	vkAllocateCommandBuffers(renderer->logical_device, &allocate_info, &command_buffer);

	VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};

	vkBeginCommandBuffer(command_buffer, &begin_info);

	VkBufferCopy copy_region = { .size = size };
	vkCmdCopyBuffer(command_buffer, src, dst, 1, &copy_region);
	vkEndCommandBuffer(command_buffer);

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &command_buffer
	};

	vkQueueSubmit(renderer->transfer_queue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(renderer->transfer_queue);

	vkFreeCommandBuffers(renderer->logical_device, renderer->transfer_command_pool, 1, &command_buffer);

	return true;
}
