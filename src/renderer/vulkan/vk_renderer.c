#include "renderer/vk_renderer.h"
#include "core/logger.h"

static uint32_t image_index = 0;

bool vulkan_renderer_create(struct arena *arena, struct platform *platform, VulkanContext *context) {
	if (vulkan_create_instance(context, platform) == false)
		return false;

	if (vulkan_create_surface(platform, context) == false)
		return false;

	if (vulkan_create_device(arena, context) == false)
		return false;

	if (vulkan_create_command_pool(context) == false)
		return false;

	if (vulkan_create_command_buffer(context) == false)
		return false;

	if (vulkan_create_sync_objects(context) == false)
		return false;

	if (vulkan_create_swapchain(context, platform) == false)
		return false;

	if (vulkan_create_depth_image(context) == false)
		return false;

	return true;
}

void vulkan_renderer_destroy(VulkanContext *context) {
}

bool vulkan_renderer_begin_frame(VulkanContext *context, struct platform *platform) {
	vkWaitForFences(context->device.logical, 1, &context->in_flight_fences[context->current_frame], VK_TRUE, UINT64_MAX);

	VkResult result = vkAcquireNextImageKHR(context->device.logical, context->swapchain.handle, UINT64_MAX, context->image_available_semaphores[context->current_frame], VK_NULL_HANDLE, &image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		vulkan_recreate_swapchain(context, platform);
		LOG_INFO("Recreating Swapchain");
		return false;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		LOG_ERROR("Failed to acquire swapchain image!");
	}

	vkResetFences(context->device.logical, 1, &context->in_flight_fences[context->current_frame]);

	vkResetCommandBuffer(context->command_buffers[context->current_frame], 0);

	VkCommandBufferBeginInfo cb_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	if (vkBeginCommandBuffer(context->command_buffers[context->current_frame], &cb_begin_info) != VK_SUCCESS) {
		LOG_ERROR("Failed to begin command buffer recording");
		return false;
	}

	VkClearValue clear_color = { .color = { .float32 = { 0.01f, 0.01f, 0.01f, 1.0f } } };
	VkClearValue clear_depth = { .depthStencil = { .depth = 1.0f, .stencil = 0 } };

	vulkan_image_transition(
		context, context->swapchain.images.handles[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	VkRenderingAttachmentInfo color_attachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = context->swapchain.images.views[image_index],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = clear_color,
	};
	VkRenderingAttachmentInfo depth_attachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = context->depth_attachment.view,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = clear_depth,
	};

	VkRenderingInfo r_info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {
		  .offset = { 0, 0 },
		  .extent = context->swapchain.extent,
		},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attachment,
		.pDepthAttachment = &depth_attachment,
	};

	vkCmdBeginRendering(context->command_buffers[context->current_frame], &r_info);
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

	vkCmdBindDescriptorSets(
		context->command_buffers[context->current_frame],
		VK_PIPELINE_BIND_POINT_GRAPHICS, context->pipeline_layout,
		0, 1, &context->descriptor_sets[context->current_frame], 0, NULL);

	return true;
}

bool Vulkan_renderer_end_frame(VulkanContext *context) {
	vkCmdEndRendering(context->command_buffers[context->current_frame]);

	vulkan_image_transition(
		context, context->swapchain.images.handles[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);

	// vkCmdEndRenderPass(context->command_buffers[context->current_frame]);
	if (vkEndCommandBuffer(context->command_buffers[context->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to record command buffer");
		return false;
	}

	VkSemaphore wait_semaphores[] = { context->image_available_semaphores[context->current_frame] };
	VkSemaphore signal_semaphores[] = { context->render_finished_semaphores[image_index] };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = array_count(wait_semaphores),
		.pWaitSemaphores = wait_semaphores,
		.pWaitDstStageMask = wait_stages,
		.commandBufferCount = 1,
		.pCommandBuffers = &context->command_buffers[context->current_frame],
		.signalSemaphoreCount = array_count(signal_semaphores),
		.pSignalSemaphores = signal_semaphores
	};

	if (vkQueueSubmit(context->device.graphics_queue, 1, &submit_info, context->in_flight_fences[context->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to submit draw command buffer");
		return false;
	}

	VkSwapchainKHR swapchains[] = { context->swapchain.handle };
	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = signal_semaphores,
		.swapchainCount = 1,
		.pSwapchains = swapchains,
		.pImageIndices = &image_index,
	};

	context->current_frame = (context->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	VkResult result = vkQueuePresentKHR(context->device.present_queue, &present_info);

	return true;
}

bool vulkan_renderer_draw(VulkanContext *context, Buffer *vertex_buffer) {
	if (vulkan_buffer_bind(context, vertex_buffer) == false) {
		LOG_WARN("Aborting draw...");
		return false;
	}

	vkCmdDraw(context->command_buffers[context->current_frame], vertex_buffer->vertex_count, 1, 0, 0);
	return true;
}

bool vulkan_renderer_draw_indexed(VulkanContext *context, Buffer *vertex_buffer, Buffer *index_buffer) {
	if (vulkan_buffer_bind(context, vertex_buffer) == false || vulkan_buffer_bind(context, index_buffer) == false) {
		LOG_WARN("Aborting indexed draw...");
		return false;
	}

	vkCmdDrawIndexed(context->command_buffers[context->current_frame], index_buffer->index_count, 1, 0, 0, 0);
	return true;
}
