#include "common.h"
#include "renderer/backend/vulkan_api.h"

#include "vk_internal.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vulkan_command_pool_create(VulkanContext *context) {
	VkCommandPoolCreateInfo cp_create_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = context->device.graphics_index
	};

	if (vkCreateCommandPool(context->device.logical, &cp_create_info, NULL, &context->graphics_command_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command pool");
		return false;
	}

	cp_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cp_create_info.queueFamilyIndex = context->device.transfer_index;
	if (vkCreateCommandPool(context->device.logical, &cp_create_info, NULL, &context->transfer_command_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command pool");
		return false;
	}

	LOG_INFO("Vulkan Command Pool created");

	return true;
}

bool vulkan_command_buffer_create(VulkanContext *context) {
	VkCommandBufferAllocateInfo cb_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = context->graphics_command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = countof(context->command_buffers)
	};

	if (vkAllocateCommandBuffers(context->device.logical, &cb_allocate_info, context->command_buffers) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command buffer");
		return false;
	}

	LOG_INFO("Vulkan Command Buffer created");
	return true;
}

bool vulkan_command_oneshot_begin(VulkanContext *context, VkCommandPool pool, VkCommandBuffer *buffer) {
	VkCommandBufferAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	if (vkAllocateCommandBuffers(context->device.logical, &allocate_info, buffer) != VK_SUCCESS) {
		LOG_ERROR("Failed to allocate VkCommandBuffer");
		return false;
	}

	VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(*buffer, &begin_info);

	return true;
}

bool vulkan_command_oneshot_end(VulkanContext *context, VkQueue queue, VkCommandPool pool, VkCommandBuffer *buffer) {
	vkEndCommandBuffer(*buffer);

	VkFence fence;
	VkFenceCreateInfo fence_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	};
	vkCreateFence(context->device.logical, &fence_info, NULL, &fence);

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = buffer
	};

	vkQueueSubmit(queue, 1, &submit_info, fence);
	vkWaitForFences(context->device.logical, 1, &fence, VK_TRUE, UINT64_MAX);

	vkFreeCommandBuffers(context->device.logical, pool, 1, buffer);
	vkDestroyFence(context->device.logical, fence, NULL);

	return true;
}
