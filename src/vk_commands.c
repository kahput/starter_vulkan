#include "vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_command_pool(struct arena *arena, VKRenderer *renderer) {
	QueueFamilyIndices indices = find_queue_families(arena, renderer);

	VkCommandPoolCreateInfo cp_create_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = indices.graphic_family
	};

	if (vkCreateCommandPool(renderer->logical_device, &cp_create_info, NULL, &renderer->command_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command pool");
		return false;
	}

	LOG_INFO("Vulkan Command Pool created successfully");

	return true;
}

bool vk_create_command_buffer(VKRenderer *renderer) {
	VkCommandBufferAllocateInfo cb_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = renderer->command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	if (vkAllocateCommandBuffers(renderer->logical_device, &cb_allocate_info, &renderer->command_buffer) != VK_SUCCESS) {
		LOG_ERROR("Failed to create command buffer");
		return false;
	}

	LOG_INFO("Vulkan Command Buffer created successfully");
	return true;
}

bool vk_record_command_buffer(uint32_t image_index, VKRenderer *renderer) {
	VkCommandBufferBeginInfo cb_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	if (vkBeginCommandBuffer(renderer->command_buffer, &cb_begin_info) != VK_SUCCESS) {
		LOG_ERROR("Failed to begin command buffer recording");
		return false;
	}

	VkClearValue clear_color = { { { 1.0f, 1.0f, 1.0f, 1.0f } } };
	VkRenderPassBeginInfo rp_begin_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = renderer->render_pass,
		.framebuffer = renderer->framebuffers[image_index],
		.renderArea.offset = { 0, 0 },
		.renderArea.extent = renderer->swapchain_extent,
		.clearValueCount = 1,
		.pClearValues = &clear_color
	};

	vkCmdBeginRenderPass(renderer->command_buffer, &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(renderer->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->graphics_pipeline);

	VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = renderer->swapchain_extent.width,
		.height = renderer->swapchain_extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(renderer->command_buffer, 0, 1, &viewport);

	VkRect2D scissor = {
		.offset = { 0.0f, 0.0f },
		.extent = renderer->swapchain_extent
	};
	vkCmdSetScissor(renderer->command_buffer, 0, 1, &scissor);

	vkCmdDraw(renderer->command_buffer, 6, 1, 0, 0);

	vkCmdEndRenderPass(renderer->command_buffer);
	if (vkEndCommandBuffer(renderer->command_buffer) != VK_SUCCESS) {
		LOG_ERROR("Failed to record command buffer");
		return false;
	}

	return true;
}
