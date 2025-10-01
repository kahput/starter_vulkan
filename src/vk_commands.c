#include "vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_command_pool(struct arena *arena, VKRenderer *renderer) {
	QueueFamilyIndices family_indices = find_queue_families(arena, renderer);

	VkCommandPoolCreateInfo cp_create_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = family_indices.graphics
	};

	if (vkCreateCommandPool(renderer->logical_device, &cp_create_info, NULL, &renderer->graphics_command_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command pool");
		return false;
	}

	cp_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cp_create_info.queueFamilyIndex = family_indices.transfer;
	if (vkCreateCommandPool(renderer->logical_device, &cp_create_info, NULL, &renderer->transfer_command_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command pool");
		return false;
	}

	LOG_INFO("Vulkan Command Pool created successfully");

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

	LOG_INFO("Vulkan Command Buffer created successfully");
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

	VkClearValue clear_color = { { { 1.0f, 1.0f, 1.0f, 1.0f } } };
	VkRenderPassBeginInfo rp_begin_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = renderer->render_pass,
		.framebuffer = renderer->framebuffers[image_index],
		.renderArea.offset = { 0, 0 },
		.renderArea.extent = renderer->swapchain.extent,
		.clearValueCount = 1,
		.pClearValues = &clear_color
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
	// vkCmdBindIndexBuffer(renderer->command_buffers[renderer->current_frame], renderer->index_buffer, 0, VK_INDEX_TYPE_UINT16);

	vkCmdBindDescriptorSets(
		renderer->command_buffers[renderer->current_frame],
		VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->pipeline_layout,
		0, 1, &renderer->descriptor_sets[renderer->current_frame], 0, NULL);

	vkCmdDraw(renderer->command_buffers[renderer->current_frame], 6, 1, 6, 0);
	// vkCmdDrawIndexed(renderer->command_buffers[renderer->current_frame], array_count(indices), 1, 0, 0, 0);

	vkCmdEndRenderPass(renderer->command_buffers[renderer->current_frame]);
	if (vkEndCommandBuffer(renderer->command_buffers[renderer->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to record command buffer");
		return false;
	}

	return true;
}
