#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_command_pool(VulkanContext *context) {
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

bool vk_create_command_buffer(VulkanContext *context) {
	VkCommandBufferAllocateInfo cb_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = context->graphics_command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = array_count(context->command_buffers)
	};

	if (vkAllocateCommandBuffers(context->device.logical, &cb_allocate_info, context->command_buffers) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command buffer");
		return false;
	}

	LOG_INFO("Vulkan Command Buffer created");
	return true;
}

bool vk_command_buffer_draw(VulkanContext *context, Buffer *vertex_buffer, uint32_t image_index) {
	VkCommandBufferBeginInfo cb_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	if (vkBeginCommandBuffer(context->command_buffers[context->current_frame], &cb_begin_info) != VK_SUCCESS) {
		LOG_ERROR("Failed to begin command buffer recording");
		return false;
	}

	VkClearValue clear_color = { .color = { .float32 = { 1.0f, 1.0f, 1.0f, 1.0f } } };
	VkClearValue clear_depth = { .depthStencil = { .depth = 1.0f, .stencil = 0 } };

	VkClearValue clear_values[] = { clear_color, clear_depth };

	VkRenderPassBeginInfo rp_begin_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context->render_pass,
		.framebuffer = context->swapchain.framebuffers[image_index],
		.renderArea.offset = { 0, 0 },
		.renderArea.extent = context->swapchain.extent,
		.clearValueCount = array_count(clear_values),
		.pClearValues = clear_values
	};

	vkCmdBeginRenderPass(context->command_buffers[context->current_frame], &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(context->command_buffers[context->current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, context->graphics_pipeline);

	VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = context->swapchain.extent.width,
		.height = context->swapchain.extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(context->command_buffers[context->current_frame], 0, 1, &viewport);

	VkRect2D scissor = {
		.offset = { 0.0f, 0.0f },
		.extent = context->swapchain.extent
	};
	vkCmdSetScissor(context->command_buffers[context->current_frame], 0, 1, &scissor);

	VkBuffer vertex_buffers[] = { ((VulkanBuffer *)vertex_buffer->internal)->handle };
	VkDeviceSize offsets[] = { 0 };

	vkCmdBindVertexBuffers(context->command_buffers[context->current_frame], 1, 1, vertex_buffers, offsets);
	// vkCmdBindIndexBuffer(context->command_buffers[context->current_frame], context->index_buffer, 0, VK_INDEX_TYPE_UINT16);

	vkCmdBindDescriptorSets(
		context->command_buffers[context->current_frame],
		VK_PIPELINE_BIND_POINT_GRAPHICS, context->pipeline_layout,
		0, 1, &context->descriptor_sets[context->current_frame], 0, NULL);

	vkCmdDraw(context->command_buffers[context->current_frame], vertex_buffer->vertex_count, 1, 0, 0);
	// vkCmdDrawIndexed(context->command_buffers[context->current_frame], array_count(indices), 1, 0, 0, 0);

	vkCmdEndRenderPass(context->command_buffers[context->current_frame]);
	if (vkEndCommandBuffer(context->command_buffers[context->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to record command buffer");
		return false;
	}

	return true;
}

bool vk_begin_single_time_commands(VulkanContext *context, VkCommandPool pool, VkCommandBuffer *buffer) {
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

bool vk_end_single_time_commands(VulkanContext *context, VkQueue queue, VkCommandPool pool, VkCommandBuffer *buffer) {
	vkEndCommandBuffer(*buffer);

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = buffer
	};

	vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(queue);

	vkFreeCommandBuffers(context->device.logical, pool, 1, buffer);

	return true;
}
