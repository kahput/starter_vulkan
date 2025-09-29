#include "core/arena.h"
#include "core/logger.h"

#include "platform.h"
#include "vk_renderer.h"

#include <stdbool.h>
#include <stdint.h>

void draw_frame(struct arena *arena, VKRenderer *renderer, struct platform *platform);
void resize_callback(struct platform *platform, uint32_t width, uint32_t height);

static bool resized = false;

int main(void) {
	Arena *vk_arena = arena_alloc();
	Arena *window_arena = arena_alloc();
	Arena *frame_arena = arena_alloc();

	uint32_t version = 0;
	vkEnumerateInstanceVersion(&version);
	logger_set_level(LOG_LEVEL_INFO);

	LOG_INFO("Vulkan %d.%d.%d", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));

	VKRenderer renderer = { 0 };
	Platform *platform = platform_startup(window_arena, 1280, 720, "Starter Vulkan");
	if (platform == NULL) {
		LOG_ERROR("Platform startup failed");
		return -1;
	}

	LOG_INFO("Successfully created wayland display");
	LOG_INFO("Logical pixel dimensions { %d, %d }", platform->logical_width, platform->logical_height);
	LOG_INFO("Physical pixel dimensions { %d, %d }", platform->physical_width, platform->physical_height);
	platform_set_physical_dimensions_callback(platform, resize_callback);

	vk_create_instance(vk_arena, &renderer, platform);
	vk_create_surface(platform, &renderer);

	vk_select_physical_device(vk_arena, &renderer);
	vk_create_logical_device(vk_arena, &renderer);

	vk_create_swapchain(vk_arena, &renderer, platform);
	vk_create_image_views(vk_arena, &renderer);
	vk_create_render_pass(&renderer);
	vk_create_framebuffers(vk_arena, &renderer);
	vk_create_graphics_pipline(vk_arena, &renderer);

	// vk_recreate_swapchain();

	vk_create_command_pool(vk_arena, &renderer);
	vk_create_vertex_buffer(vk_arena,  &renderer);
	vk_create_command_buffer(&renderer);
	vk_create_sync_objects(&renderer);

	while (platform_should_close(platform) == false) {
		platform_poll_events(platform);
		draw_frame(frame_arena, &renderer, platform);
	}

	vkDeviceWaitIdle(renderer.logical_device);

	return 0;
}

void draw_frame(struct arena *arena, VKRenderer *renderer, struct platform *platform) {
	vkWaitForFences(renderer->logical_device, 1, &renderer->in_flight_fences[renderer->current_frame], VK_TRUE, UINT64_MAX);

	uint32_t image_index = 0;
	VkResult result = vkAcquireNextImageKHR(renderer->logical_device, renderer->swapchain.handle, UINT64_MAX, renderer->image_available_semaphores[renderer->current_frame], VK_NULL_HANDLE, &image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		vk_recreate_swapchain(arena, renderer, platform);
		LOG_INFO("Recreating Swapchain");
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		LOG_ERROR("Failed to acquire swapchain image!");
	}

	vkResetFences(renderer->logical_device, 1, &renderer->in_flight_fences[renderer->current_frame]);

	vkResetCommandBuffer(renderer->command_buffers[renderer->current_frame], 0);
	vk_record_command_buffers(renderer, image_index);

	VkSemaphore wait_semaphores[] = { renderer->image_available_semaphores[renderer->current_frame] };
	VkSemaphore signal_semaphores[] = { renderer->render_finished_semaphores[image_index] };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = array_count(wait_semaphores),
		.pWaitSemaphores = wait_semaphores,
		.pWaitDstStageMask = wait_stages,
		.commandBufferCount = 1,
		.pCommandBuffers = &renderer->command_buffers[renderer->current_frame],
		.signalSemaphoreCount = array_count(signal_semaphores),
		.pSignalSemaphores = signal_semaphores
	};

	if (vkQueueSubmit(renderer->graphics_queue, 1, &submit_info, renderer->in_flight_fences[renderer->current_frame]) != VK_SUCCESS) {
		LOG_ERROR("Failed to submit draw command buffer");
		return;
	}

	VkSwapchainKHR swapchains[] = { renderer->swapchain.handle };
	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = signal_semaphores,
		.swapchainCount = 1,
		.pSwapchains = swapchains,
		.pImageIndices = &image_index,
	};

	renderer->current_frame = (renderer->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	result = vkQueuePresentKHR(renderer->present_queue, &present_info);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || resized) {
		vk_recreate_swapchain(arena, renderer, platform);
		LOG_INFO("Recreating Swapchain");
		resized = false;
		return;
	} else if (result != VK_SUCCESS) {
		LOG_ERROR("Failed to acquire swapchain image!");
		return;
	}
}

void resize_callback(struct platform *platform, uint32_t width, uint32_t height) {
	resized = true;
}
