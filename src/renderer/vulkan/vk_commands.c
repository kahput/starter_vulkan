#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_command_pool(VulkanContext *ctx) {
	VkCommandPoolCreateInfo cp_create_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = ctx->device.graphics_index
	};

	if (vkCreateCommandPool(ctx->device.logical, &cp_create_info, NULL, &ctx->graphics_command_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command pool");
		return false;
	}

	cp_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cp_create_info.queueFamilyIndex = ctx->device.transfer_index;
	if (vkCreateCommandPool(ctx->device.logical, &cp_create_info, NULL, &ctx->transfer_command_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command pool");
		return false;
	}

	LOG_INFO("Vulkan Command Pool created");

	return true;
}

bool vk_create_command_buffer(VulkanContext *ctx) {
	VkCommandBufferAllocateInfo cb_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = ctx->graphics_command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = array_count(ctx->command_buffers)
	};

	if (vkAllocateCommandBuffers(ctx->device.logical, &cb_allocate_info, ctx->command_buffers) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command buffer");
		return false;
	}

	LOG_INFO("Vulkan Command Buffer created");
	return true;
}

bool vk_record_command_buffers(VulkanContext *ctx, uint32_t image_index) {
	VkCommandBufferBeginInfo cb_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	if (vkBeginCommandBuffer(ctx->command_buffers[ctx->current_frame], &cb_begin_info) != VK_SUCCESS) {
		LOG_ERROR("Failed to begin command buffer recording");
		return false;
	}

	VkClearValue clear_color = { .color = { .float32 = { 1.0f, 1.0f, 1.0f, 1.0f } } };
	VkClearValue clear_depth = { .depthStencil = { .depth = 1.0f, .stencil = 0 } };

	VkClearValue clear_values[] = { clear_color, clear_depth };

	VkRenderPassBeginInfo rp_begin_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = ctx->render_pass,
		.framebuffer = ctx->swapchain.framebuffers[image_index],
		.renderArea.offset = { 0, 0 },
		.renderArea.extent = ctx->swapchain.extent,
		.clearValueCount = array_count(clear_values),
		.pClearValues = clear_values
	};

	vkCmdBeginRenderPass(ctx->command_buffers[ctx->current_frame], &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(ctx->command_buffers[ctx->current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->graphics_pipeline);

	VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = ctx->swapchain.extent.width,
		.height = ctx->swapchain.extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(ctx->command_buffers[ctx->current_frame], 0, 1, &viewport);

	VkRect2D scissor = {
		.offset = { 0.0f, 0.0f },
		.extent = ctx->swapchain.extent
	};
	vkCmdSetScissor(ctx->command_buffers[ctx->current_frame], 0, 1, &scissor);

	VkBuffer vertex_buffers[] = { ctx->vertex_buffer };
	VkDeviceSize offsets[] = { 0 };

	vkCmdBindVertexBuffers(ctx->command_buffers[ctx->current_frame], 1, 1, vertex_buffers, offsets);
	// vkCmdBindIndexBuffer(ctx->command_buffers[ctx->current_frame], ctx->index_buffer, 0, VK_INDEX_TYPE_UINT16);

	vkCmdBindDescriptorSets(
		ctx->command_buffers[ctx->current_frame],
		VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline_layout,
		0, 1, &ctx->descriptor_sets[ctx->current_frame], 0, NULL);

	vkCmdDraw(ctx->command_buffers[ctx->current_frame], array_count(vertices), 1, 0, 0);
	// vkCmdDrawIndexed(ctx->command_buffers[ctx->current_frame], array_count(indices), 1, 0, 0, 0);

	vkCmdEndRenderPass(ctx->command_buffers[ctx->current_frame]);
	if (vkEndCommandBuffer(ctx->command_buffers[ctx->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to record command buffer");
		return false;
	}

	return true;
}

bool vk_begin_single_time_commands(VulkanContext *ctx, VkCommandPool pool, VkCommandBuffer *buffer) {
	VkCommandBufferAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	if (vkAllocateCommandBuffers(ctx->device.logical, &allocate_info, buffer) != VK_SUCCESS) {
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

bool vk_end_single_time_commands(VulkanContext *ctx, VkQueue queue, VkCommandPool pool, VkCommandBuffer *buffer) {
	vkEndCommandBuffer(*buffer);

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = buffer
	};

	vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(queue);

	vkFreeCommandBuffers(ctx->device.logical, pool, 1, buffer);

	return true;
}
