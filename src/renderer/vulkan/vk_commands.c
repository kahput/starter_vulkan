#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_command_pool(VulkanState *vk_state) {
	VkCommandPoolCreateInfo cp_create_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = vk_state->device.queue_indices.graphics
	};

	if (vkCreateCommandPool(vk_state->device.logical, &cp_create_info, NULL, &vk_state->graphics_command_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command pool");
		return false;
	}

	cp_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cp_create_info.queueFamilyIndex = vk_state->device.queue_indices.transfer;
	if (vkCreateCommandPool(vk_state->device.logical, &cp_create_info, NULL, &vk_state->transfer_command_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command pool");
		return false;
	}

	LOG_INFO("Vulkan Command Pool created");

	return true;
}

bool vk_create_command_buffer(VulkanState *vk_state) {
	VkCommandBufferAllocateInfo cb_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = vk_state->graphics_command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = array_count(vk_state->command_buffers)
	};

	if (vkAllocateCommandBuffers(vk_state->device.logical, &cb_allocate_info, vk_state->command_buffers) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command buffer");
		return false;
	}

	LOG_INFO("Vulkan Command Buffer created");
	return true;
}

bool vk_record_command_buffers(VulkanState *vk_state, uint32_t image_index) {
	VkCommandBufferBeginInfo cb_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	if (vkBeginCommandBuffer(vk_state->command_buffers[vk_state->current_frame], &cb_begin_info) != VK_SUCCESS) {
		LOG_ERROR("Failed to begin command buffer recording");
		return false;
	}

	VkClearValue clear_color = { .color = { .float32 = { 1.0f, 1.0f, 1.0f, 1.0f } } };
	VkClearValue clear_depth = { .depthStencil = { .depth = 1.0f, .stencil = 0 } };

	VkClearValue clear_values[] = { clear_color, clear_depth };

	VkRenderPassBeginInfo rp_begin_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = vk_state->render_pass,
		.framebuffer = vk_state->swapchain.framebuffers[image_index],
		.renderArea.offset = { 0, 0 },
		.renderArea.extent = vk_state->swapchain.extent,
		.clearValueCount = array_count(clear_values),
		.pClearValues = clear_values
	};

	vkCmdBeginRenderPass(vk_state->command_buffers[vk_state->current_frame], &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(vk_state->command_buffers[vk_state->current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, vk_state->graphics_pipeline);

	VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = vk_state->swapchain.extent.width,
		.height = vk_state->swapchain.extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(vk_state->command_buffers[vk_state->current_frame], 0, 1, &viewport);

	VkRect2D scissor = {
		.offset = { 0.0f, 0.0f },
		.extent = vk_state->swapchain.extent
	};
	vkCmdSetScissor(vk_state->command_buffers[vk_state->current_frame], 0, 1, &scissor);

	VkBuffer vertex_buffers[] = { vk_state->vertex_buffer };
	VkDeviceSize offsets[] = { 0 };

	vkCmdBindVertexBuffers(vk_state->command_buffers[vk_state->current_frame], 1, 1, vertex_buffers, offsets);
	// vkCmdBindIndexBuffer(vk_state->command_buffers[vk_state->current_frame], vk_state->index_buffer, 0, VK_INDEX_TYPE_UINT16);

	vkCmdBindDescriptorSets(
		vk_state->command_buffers[vk_state->current_frame],
		VK_PIPELINE_BIND_POINT_GRAPHICS, vk_state->pipeline_layout,
		0, 1, &vk_state->descriptor_sets[vk_state->current_frame], 0, NULL);

	vkCmdDraw(vk_state->command_buffers[vk_state->current_frame], array_count(vertices), 1, 0, 0);
	// vkCmdDrawIndexed(vk_state->command_buffers[vk_state->current_frame], array_count(indices), 1, 0, 0, 0);

	vkCmdEndRenderPass(vk_state->command_buffers[vk_state->current_frame]);
	if (vkEndCommandBuffer(vk_state->command_buffers[vk_state->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to record command buffer");
		return false;
	}

	return true;
}

bool vk_begin_single_time_commands(VulkanState *vk_state, VkCommandPool pool, VkCommandBuffer *buffer) {
	VkCommandBufferAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	if (vkAllocateCommandBuffers(vk_state->device.logical, &allocate_info, buffer) != VK_SUCCESS) {
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

bool vk_end_single_time_commands(VulkanState *vk_state, VkQueue queue, VkCommandPool pool, VkCommandBuffer *buffer) {
	vkEndCommandBuffer(*buffer);

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = buffer
	};

	vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(queue);

	vkFreeCommandBuffers(vk_state->device.logical, pool, 1, buffer);

	return true;
}
