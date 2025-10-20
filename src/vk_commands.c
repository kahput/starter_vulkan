#include "vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_command_pool(struct arena *arena, VKRenderer *renderer) {
	VkCommandPoolCreateInfo cp_create_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = renderer->family_indices.graphics
	};

	if (vkCreateCommandPool(renderer->logical_device, &cp_create_info, NULL, &renderer->graphics_command_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command pool");
		return false;
	}

	cp_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cp_create_info.queueFamilyIndex = renderer->family_indices.transfer;
	if (vkCreateCommandPool(renderer->logical_device, &cp_create_info, NULL, &renderer->transfer_command_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command pool");
		return false;
	}

	LOG_INFO("Vulkan Command Pool created");

	return true;
}

bool vk_create_command_buffer(VKRenderer *renderer) {
	VkCommandBufferAllocateInfo cb_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = renderer->graphics_command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = array_count(renderer->command_buffers)
	};

	if (vkAllocateCommandBuffers(renderer->logical_device, &cb_allocate_info, renderer->command_buffers) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command buffer");
		return false;
	}

	LOG_INFO("Vulkan Command Buffer created");
	return true;
}

bool vk_record_command_buffers(VKRenderer *renderer, uint32_t image_index) {
	VkCommandBufferBeginInfo cb_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	if (vkBeginCommandBuffer(renderer->command_buffers[renderer->current_frame], &cb_begin_info) != VK_SUCCESS) {
		LOG_ERROR("Failed to begin command buffer recording");
		return false;
	}

	VkClearValue clear_color = { .color = { .float32 = { 1.0f, 1.0f, 1.0f, 1.0f } } };
	VkClearValue clear_depth = { .depthStencil = { .depth = 1.0f, .stencil = 0 } };

	VkClearValue clear_values[] = { clear_color, clear_depth };

	VkRenderPassBeginInfo rp_begin_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = renderer->render_pass,
		.framebuffer = renderer->framebuffers[image_index],
		.renderArea.offset = { 0, 0 },
		.renderArea.extent = renderer->swapchain.extent,
		.clearValueCount = array_count(clear_values),
		.pClearValues = clear_values
	};

	vkCmdBeginRenderPass(renderer->command_buffers[renderer->current_frame], &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(renderer->command_buffers[renderer->current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->graphics_pipeline);

	VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = renderer->swapchain.extent.width,
		.height = renderer->swapchain.extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(renderer->command_buffers[renderer->current_frame], 0, 1, &viewport);

	VkRect2D scissor = {
		.offset = { 0.0f, 0.0f },
		.extent = renderer->swapchain.extent
	};
	vkCmdSetScissor(renderer->command_buffers[renderer->current_frame], 0, 1, &scissor);

	VkBuffer vertex_buffers[] = { renderer->vertex_buffer };
	VkDeviceSize offsets[] = { 0 };

	vkCmdBindVertexBuffers(renderer->command_buffers[renderer->current_frame], 1, 1, vertex_buffers, offsets);
	vkCmdBindIndexBuffer(renderer->command_buffers[renderer->current_frame], renderer->index_buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdBindDescriptorSets(
		renderer->command_buffers[renderer->current_frame],
		VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->pipeline_layout,
		0, 1, &renderer->descriptor_sets[renderer->current_frame], 0, NULL);

	// vkCmdDraw(renderer->command_buffers[renderer->current_frame], 36, 1, 0, 0);
	vkCmdDrawIndexed(renderer->command_buffers[renderer->current_frame], 36, 1, 0, 0, 0);

	vkCmdEndRenderPass(renderer->command_buffers[renderer->current_frame]);
	if (vkEndCommandBuffer(renderer->command_buffers[renderer->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to record command buffer");
		return false;
	}

	return true;
}

bool vk_begin_single_time_commands(VKRenderer *renderer, VkCommandPool pool, VkCommandBuffer *buffer) {
	VkCommandBufferAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	if (vkAllocateCommandBuffers(renderer->logical_device, &allocate_info, buffer) != VK_SUCCESS) {
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

bool vk_end_single_time_commands(VKRenderer *renderer, VkQueue queue, VkCommandPool pool, VkCommandBuffer *buffer) {
	vkEndCommandBuffer(*buffer);

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = buffer
	};

	vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(queue);

	vkFreeCommandBuffers(renderer->logical_device, pool, 1, buffer);

	return true;
}
