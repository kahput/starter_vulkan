#include "renderer/backend/vulkan_api.h"
#include "vk_internal.h"

#include "platform.h"

#include "allocators/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

static uint32_t image_index = 0;

bool vulkan_renderer_create(struct arena *arena, struct platform *platform, VulkanContext **out_context) {
	*out_context = arena_push_struct(arena, VulkanContext);

	VulkanContext *ctx = *out_context;

	uint32_t version = 0;
	vkEnumerateInstanceVersion(&version);

	LOG_INFO("Vulkan %d.%d.%d", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));

	// Index into these
	ctx->image_pool = arena_push_array_zero(arena, VulkanImage, MAX_TEXTURES);
	ctx->buffer_pool = arena_push_array_zero(arena, VulkanBuffer, MAX_BUFFERS);
	ctx->sampler_pool = arena_push_array_zero(arena, VulkanSampler, MAX_SAMPLERS);
	ctx->shader_pool = arena_push_array_zero(arena, VulkanShader, MAX_SHADERS);

	if (vulkan_instance_create(ctx, platform) == false)
		return false;

	if (vulkan_surface_create(platform, ctx) == false)
		return false;

	if (vulkan_device_create(arena, ctx) == false)
		return false;

	if (vulkan_command_pool_create(ctx) == false)
		return false;

	if (vulkan_command_buffer_create(ctx) == false)
		return false;

	if (vulkan_descriptor_pool_create(ctx) == false)
		return false;

	if (vulkan_sync_objects_create(ctx) == false)
		return false;

	if (vulkan_swapchain_create(ctx, platform->physical_width, platform->physical_height) == false)
		return false;

	if (vulkan_create_depth_image(ctx) == false)
		return false;

	// TODO: Let the user decide
	if (vulkan_descriptor_global_create(ctx) == false)
		return false;

	return true;
}

void vulkan_renderer_destroy(VulkanContext *context) {
	vkDeviceWaitIdle(context->device.logical);

	vkDestroyDescriptorPool(context->device.logical, context->descriptor_pool, NULL);
	for (uint32_t index = 0; index < MAX_SHADERS; ++index) {
		if (context->shader_pool[index].vertex_shader != NULL)
			vulkan_renderer_shader_destroy(context, index);
	}

	for (uint32_t index = 0; index < MAX_TEXTURES; ++index) {
		if (context->image_pool[index].handle)
			vulkan_renderer_texture_destroy(context, index);
	}

	for (uint32_t index = 0; index < MAX_SAMPLERS; ++index) {
		if (context->sampler_pool[index].handle)
			vulkan_renderer_sampler_destroy(context, index);
	}

	for (uint32_t index = 0; index < MAX_BUFFERS; ++index) {
		if (context->buffer_pool[index].handle[0] != NULL)
			vulkan_renderer_buffer_destroy(context, index);
	}

	vkDestroyCommandPool(context->device.logical, context->graphics_command_pool, NULL);
	vkDestroyCommandPool(context->device.logical, context->transfer_command_pool, NULL);

	for (uint32_t index = 0; index < context->swapchain.images.count; ++index) {
		vkDestroyImageView(context->device.logical, context->swapchain.images.views[index], NULL);
	}

	vkDestroyImageView(context->device.logical, context->depth_attachment.view, NULL);
	vkDestroyImage(context->device.logical, context->depth_attachment.handle, NULL);
	vkFreeMemory(context->device.logical, context->depth_attachment.memory, NULL);

	vkDestroySwapchainKHR(context->device.logical, context->swapchain.handle, NULL);

	for (uint32_t index = 0; index < MAX_FRAMES_IN_FLIGHT; ++index) {
		vkDestroySemaphore(context->device.logical, context->render_finished_semaphores[index], NULL);
		vkDestroySemaphore(context->device.logical, context->image_available_semaphores[index], NULL);
		vkDestroyFence(context->device.logical, context->in_flight_fences[index], NULL);
	}

#ifndef NDEBUG
	vkDestroyDebugUtilsMessenger(context->instance, context->debug_messenger, NULL);
#endif

	vkDestroyDevice(context->device.logical, NULL);
	vkDestroySurfaceKHR(context->instance, context->surface, NULL);
	vkDestroyInstance(context->instance, NULL);
}

bool vulkan_renderer_on_resize(VulkanContext *context, uint32_t new_width, uint32_t new_height) {
	LOG_INFO("Recreating Swapchain...");
	logger_indent();
	if (vulkan_swapchain_recreate(context, new_width, new_height) == true) {
		LOG_INFO("Swapchain successfully recreated");
	} else {
		LOG_WARN("Failed to recreate swapchain");
	}

	logger_dedent();
	return true;
}

bool vulkan_renderer_frame_begin(VulkanContext *context, struct platform *platform) {
	vkWaitForFences(context->device.logical, 1, &context->in_flight_fences[context->current_frame], VK_TRUE, UINT64_MAX);

	VkResult result = vkAcquireNextImageKHR(context->device.logical, context->swapchain.handle, UINT64_MAX, context->image_available_semaphores[context->current_frame], VK_NULL_HANDLE, &image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		vulkan_swapchain_recreate(context, platform->physical_width, platform->physical_height);
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

	vulkan_image_transition_inline(
		context, context->command_buffers[context->current_frame],
		context->swapchain.images.handles[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
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

	return true;
}

bool Vulkan_renderer_frame_end(VulkanContext *context) {
	vkCmdEndRendering(context->command_buffers[context->current_frame]);

	vulkan_image_transition_inline(
		context, context->command_buffers[context->current_frame],
		context->swapchain.images.handles[image_index], VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);

	if (vkEndCommandBuffer(context->command_buffers[context->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to record command buffer");
		return false;
	}

	VkSemaphore wait_semaphores[] = { context->image_available_semaphores[context->current_frame] };
	VkSemaphore signal_semaphores[] = { context->render_finished_semaphores[image_index] };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = countof(wait_semaphores),
		.pWaitSemaphores = wait_semaphores,
		.pWaitDstStageMask = wait_stages,
		.commandBufferCount = 1,
		.pCommandBuffers = &context->command_buffers[context->current_frame],
		.signalSemaphoreCount = countof(signal_semaphores),
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

bool vulkan_renderer_draw(VulkanContext *context, uint32_t vertex_count) {
	vkCmdDraw(context->command_buffers[context->current_frame], vertex_count, 1, 0, 0);
	return true;
}

bool vulkan_renderer_draw_indexed(VulkanContext *context, uint32_t index_count) {
	vkCmdDrawIndexed(context->command_buffers[context->current_frame], index_count, 1, 0, 0, 0);
	return true;
}
