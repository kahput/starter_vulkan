#include "renderer/vk_renderer.h"

#include "core/logger.h"
#include <string.h>
#include <vulkan/vulkan_core.h>

// clang-format off
// clang-format off
const Vertex vertices[] = {
    // Front (+Z)
    { .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }}, // Bottom-left
    { .position = {  0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }}, // Bottom-right
    { .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }}, // Top-right
    { .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }},
    { .position = { -0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 1.0f }},
    { .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},

    // Back (-Z)
    { .position = {  0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }}, // Bottom-right
    { .position = { -0.5f, -0.5f, -0.5f }, .uv = { 1.0f, 0.0f }}, // Bottom-left
    { .position = { -0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }}, // Top-left
    { .position = { -0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
    { .position = {  0.5f,  0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
    { .position = {  0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }},

    // Left (-X)
    { .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }},
    { .position = { -0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
    { .position = { -0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }},
    { .position = { -0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }},
    { .position = { -0.5f,  0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
    { .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }},

    // Right (+X)
    { .position = {  0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
    { .position = {  0.5f, -0.5f, -0.5f }, .uv = { 1.0f, 0.0f }},
    { .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
    { .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
    { .position = {  0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 1.0f }},
    { .position = {  0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},

    // Bottom (-Y)
    { .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
    { .position = {  0.5f, -0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
    { .position = {  0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
    { .position = {  0.5f, -0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
    { .position = { -0.5f, -0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
    { .position = { -0.5f, -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},

    // Top (+Y)
    { .position = { -0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 0.0f }},
    { .position = {  0.5f,  0.5f,  0.5f }, .uv = { 1.0f, 0.0f }},
    { .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
    { .position = {  0.5f,  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }},
    { .position = { -0.5f,  0.5f, -0.5f }, .uv = { 0.0f, 1.0f }},
    { .position = { -0.5f,  0.5f,  0.5f }, .uv = { 0.0f, 0.0f }}
};
// clang-format on

const uint16_t indices[6] = {
	0, 1, 2, 2, 3, 0
};

bool vk_create_vertex_buffer(VulkanState *vk_state) {
	VkDeviceSize size = sizeof(vertices);

	VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;

	vk_create_buffer(vk_state, vk_state->device.queue_indices, size, staging_usage, staging_properties, &staging_buffer, &staging_buffer_memory);

	void *data;
	vkMapMemory(vk_state->device.logical, staging_buffer_memory, 0, size, 0, &data);
	memcpy(data, vertices, sizeof(vertices));
	vkUnmapMemory(vk_state->device.logical, staging_buffer_memory);

	vk_create_buffer(vk_state, vk_state->device.queue_indices, size, usage, properties, &vk_state->vertex_buffer, &vk_state->vertex_buffer_memory);

	vk_copy_buffer(vk_state, staging_buffer, vk_state->vertex_buffer, size);
	vkDestroyBuffer(vk_state->device.logical, staging_buffer, NULL);
	vkFreeMemory(vk_state->device.logical, staging_buffer_memory, NULL);

	return true;
}

bool vk_create_index_buffer(VulkanState *vk_state) {
	VkDeviceSize size = sizeof(indices);

	VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;

	vk_create_buffer(vk_state, vk_state->device.queue_indices, size, staging_usage, staging_properties, &staging_buffer, &staging_buffer_memory);

	void *data;
	vkMapMemory(vk_state->device.logical, staging_buffer_memory, 0, size, 0, &data);
	memcpy(data, indices, sizeof(indices));
	vkUnmapMemory(vk_state->device.logical, staging_buffer_memory);

	vk_create_buffer(vk_state, vk_state->device.queue_indices, size, usage, properties, &vk_state->index_buffer, &vk_state->index_buffer_memory);

	vk_copy_buffer(vk_state, staging_buffer, vk_state->index_buffer, size);
	vkDestroyBuffer(vk_state->device.logical, staging_buffer, NULL);
	vkFreeMemory(vk_state->device.logical, staging_buffer_memory, NULL);

	return true;
}

bool vk_create_uniform_buffers(VulkanState *vk_state) {
	VkDeviceSize size = sizeof(MVPObject);

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		vk_create_buffer(vk_state, vk_state->device.queue_indices, size, usage, properties, vk_state->uniform_buffers + i, vk_state->uniform_buffers_memory + i);

		vkMapMemory(vk_state->device.logical, vk_state->uniform_buffers_memory[i], 0, size, 0, &vk_state->uniform_buffers_mapped[i]);
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
	VulkanState *vk_state,
	QueueFamilyIndices queue_familes,
	VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	VkBuffer *buffer, VkDeviceMemory *buffer_memory) {
	uint32_t family_indices[] = { queue_familes.graphics, queue_familes.transfer };

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

	if (vkCreateBuffer(vk_state->device.logical, &vb_create_info, NULL, buffer) != VK_SUCCESS) {
		LOG_ERROR("Failed to create vertex buffer");
		return false;
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(vk_state->device.logical, *buffer, &memory_requirements);

	VkMemoryAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = find_memory_type(vk_state->device.physical, memory_requirements.memoryTypeBits, properties),
	};

	if (vkAllocateMemory(vk_state->device.logical, &allocate_info, NULL, buffer_memory) != VK_SUCCESS) {
		LOG_ERROR("Failed to allocate vertex buffer memory");
		return false;
	}

	vkBindBufferMemory(vk_state->device.logical, *buffer, *buffer_memory, 0);

	LOG_INFO("VkBuffer created");

	return true;
}

bool vk_copy_buffer(VulkanState *vk_state, VkBuffer src, VkBuffer dst, VkDeviceSize size) {
	VkCommandBuffer command_buffer;
	vk_begin_single_time_commands(vk_state, vk_state->transfer_command_pool, &command_buffer);

	VkBufferCopy copy_region = { .size = size };
	vkCmdCopyBuffer(command_buffer, src, dst, 1, &copy_region);

	vk_end_single_time_commands(vk_state, vk_state->device.transfer_queue, vk_state->transfer_command_pool, &command_buffer);
	return true;
}
