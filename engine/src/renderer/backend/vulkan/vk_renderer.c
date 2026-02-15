#include "renderer/r_internal.h"
#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "platform.h"

#include "core/pool.h"
#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vulkan_renderer_create(struct arena *arena, struct platform *display, VulkanContext **out_context) {
	*out_context = arena_push_struct(arena, VulkanContext);

	VulkanContext *context = *out_context;
	context->display = display;

	uint32_t version = 0;
	vkEnumerateInstanceVersion(&version);

	LOG_INFO("Vulkan %d.%d.%d", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));

	context->image_pool = arena_push_pool_zero(arena, VulkanImage, MAX_TEXTURES);
	context->buffer_pool = arena_push_pool_zero(arena, VulkanBuffer, MAX_BUFFERS);
	context->sampler_pool = arena_push_pool_zero(arena, VulkanSampler, MAX_SAMPLERS);
	context->shader_pool = arena_push_pool_zero(arena, VulkanShader, MAX_SHADERS);
	context->set_pool = arena_push_pool_zero(arena, VulkanUniformSet, MAX_UNIFORM_SETS);

	// 0 == INVALID
	pool_alloc(context->image_pool);
	pool_alloc(context->buffer_pool);
	pool_alloc(context->sampler_pool);
	pool_alloc(context->shader_pool);
	pool_alloc(context->set_pool);

	if (vulkan_instance_create(context, context->display) == false)
		return false;

	if (vulkan_surface_create(context, context->display) == false)
		return false;

	if (vulkan_device_create(arena, context) == false)
		return false;

	if (vulkan_command_pool_create(context) == false)
		return false;

	if (vulkan_command_buffer_create(context) == false)
		return false;

	if (vulkan_descriptor_pool_create(context) == false)
		return false;

	if (vulkan_sync_objects_create(context) == false)
		return false;

	uint32_t width, height;
	platform_get_physical_dimensions(context->display, &width, &height);
	if (vulkan_swapchain_create(context, width, height) == false)
		return false;

	// TODO: Lower this back down to 32?
	if (vulkan_buffer_create(
			context, MiB(256), MAX_FRAMES_IN_FLIGHT,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&context->staging_buffer) == false)
		return false;
	vulkan_buffer_memory_map(context, &context->staging_buffer);

	context->global_range = (VkPushConstantRange){
		.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
		.offset = 0,
		.size = 128
	};

	return true;
}

void vulkan_renderer_destroy(VulkanContext *context) {
	vkDeviceWaitIdle(context->device.logical);
	vkDestroyDescriptorPool(context->device.logical, context->descriptor_pool, NULL);

	for (uint32_t index = 0; index < MAX_SHADERS; ++index) {
		if (context->shader_pool[index].state == VULKAN_RESOURCE_STATE_INITIALIZED)
			vulkan_renderer_shader_destroy(context, (RhiShader){ index });
	}

	for (uint32_t index = 0; index < MAX_TEXTURES; ++index) {
		if (context->image_pool[index].state == VULKAN_RESOURCE_STATE_INITIALIZED)
			vulkan_renderer_texture_destroy(context, (RhiTexture){ index });
	}

	for (uint32_t index = 0; index < MAX_SAMPLERS; ++index) {
		if (context->sampler_pool[index].state == VULKAN_RESOURCE_STATE_INITIALIZED)
			vulkan_renderer_sampler_destroy(context, (RhiSampler){ index });
	}

	for (uint32_t index = 0; index < MAX_BUFFERS; ++index) {
		if (context->buffer_pool[index].state == VULKAN_RESOURCE_STATE_INITIALIZED)
			vulkan_renderer_buffer_destroy(context, (RhiBuffer){ index });
	}

	for (uint32_t index = 0; index < MAX_UNIFORM_SETS; ++index) {
		if (context->set_pool[index].state == VULKAN_RESOURCE_STATE_INITIALIZED)
			vulkan_renderer_uniform_set_destroy(context, (RhiUniformSet){ index });
	}

	vkDestroyBuffer(context->device.logical, context->staging_buffer.handle, NULL);
	vkFreeMemory(context->device.logical, context->staging_buffer.memory, NULL);
	context->staging_buffer = (VulkanBuffer){ 0 };

	vkDestroyCommandPool(context->device.logical, context->graphics_command_pool, NULL);
	vkDestroyCommandPool(context->device.logical, context->transfer_command_pool, NULL);

	for (uint32_t index = 0; index < context->swapchain.images.count; ++index) {
		vkDestroyImageView(context->device.logical, context->swapchain.images.views[index], NULL);
	}

	vkDestroySwapchainKHR(context->device.logical, context->swapchain.handle, NULL);
	for (uint32_t color_index = 0; color_index < MAX_COLOR_ATTACHMENTS; ++color_index)
		vulkan_image_destroy(context, &context->msaa_colors[color_index]);
	vulkan_image_destroy(context, &context->msaa_depth);

	for (uint32_t index = 0; index < MAX_FRAMES_IN_FLIGHT; ++index) {
		vkDestroySemaphore(context->device.logical, context->image_available_semaphores[index], NULL);
		vkDestroyFence(context->device.logical, context->in_flight_fences[index], NULL);
	}

	for (uint32_t index = 0; index < SWAPCHAIN_IMAGE_COUNT; ++index)
		vkDestroySemaphore(context->device.logical, context->render_finished_semaphores[index], NULL);

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

bool vulkan_renderer_frame_begin(VulkanContext *context, uint32_t width, uint32_t height) {
	vkWaitForFences(context->device.logical, 1, &context->in_flight_fences[context->current_frame], VK_TRUE, UINT64_MAX);

	VkResult result = vkAcquireNextImageKHR(context->device.logical, context->swapchain.handle, UINT64_MAX, context->image_available_semaphores[context->current_frame], VK_NULL_HANDLE, &context->image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		vulkan_swapchain_recreate(context, width, height);
		LOG_INFO("Recreating Swapchain");
		return false;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		LOG_ERROR("Failed to acquire swapchain image!");
	}

	vkResetFences(context->device.logical, 1, &context->in_flight_fences[context->current_frame]);

	vkResetCommandBuffer(context->command_buffers[context->current_frame], 0);
	context->staging_buffer.offset = 0;

	VkCommandBufferBeginInfo cb_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	if (vkBeginCommandBuffer(context->command_buffers[context->current_frame], &cb_begin_info) != VK_SUCCESS) {
		LOG_ERROR("Failed to begin command buffer recording");
		return false;
	}

	vulkan_image_transition(
		context, context->command_buffers[context->current_frame],
		context->swapchain.images.handles[context->image_index], VK_IMAGE_ASPECT_COLOR_BIT, 1,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	return true;
}

bool Vulkan_renderer_frame_end(VulkanContext *context) {
	vulkan_image_transition(
		context, context->command_buffers[context->current_frame],
		context->swapchain.images.handles[context->image_index], VK_IMAGE_ASPECT_COLOR_BIT, 1,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);

	if (vkEndCommandBuffer(context->command_buffers[context->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to record command buffer");
		return false;
	}

	VkSemaphore wait_semaphores[] = { context->image_available_semaphores[context->current_frame] };
	VkSemaphore signal_semaphores[] = { context->render_finished_semaphores[context->image_index] };
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
		.pImageIndices = &context->image_index,
	};

	context->current_frame = (context->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	VkResult result = vkQueuePresentKHR(context->device.present_queue, &present_info);

	// NOTE: All bound resources are unbound after frame ends
	context->bound_shader = NULL;

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
