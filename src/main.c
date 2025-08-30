#include "core/arena.h"
#include "core/logger.h"

#include "platform.h"
#include "vk_renderer.h"

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

void draw_frame(struct arena *arena, VKRenderer *renderer);

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
	vk_create_instance(vk_arena, &renderer, platform);
	vk_load_extensions(&renderer);
	vk_create_surface(platform, &renderer);
	vk_select_physical_device(vk_arena, &renderer);
	vk_create_logical_device(vk_arena, &renderer);
	vk_create_swapchain(vk_arena, &renderer, platform);
	vk_create_image_views(vk_arena, &renderer);
	vk_create_render_pass(&renderer);
	vk_create_graphics_pipline(vk_arena, &renderer);
	vk_create_framebuffers(vk_arena, &renderer);
	vk_create_command_pool(vk_arena, &renderer);
	vk_create_command_buffer(&renderer);
	vk_create_sync_objects(&renderer);

	while (platform_should_close(platform) == false) {
		platform_poll_events(platform);
		draw_frame(frame_arena, &renderer);
	}

	return 0;
}

void draw_frame(struct arena *arena, VKRenderer *renderer) {
	vkWaitForFences(renderer->logical_device, 1, &renderer->in_flight_fence, VK_TRUE, UINT64_MAX);
	vkResetFences(renderer->logical_device, 1, &renderer->in_flight_fence);

	uint32_t image_index = 0;
	vkAcquireNextImageKHR(renderer->logical_device, renderer->swapchain, UINT64_MAX, renderer->image_available_semaphore, VK_NULL_HANDLE, &image_index);

	vkResetCommandBuffer(renderer->command_buffer, 0);
	vk_record_command_buffer(image_index, renderer);

	VkSemaphore wait_semaphores[] = { renderer->image_available_semaphore };
	VkSemaphore signal_semaphores[] = { renderer->render_finished_semaphore };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = array_count(wait_semaphores),
		.pWaitSemaphores = wait_semaphores,
		.pWaitDstStageMask = wait_stages,
		.commandBufferCount = 1,
		.pCommandBuffers = &renderer->command_buffer,
		.signalSemaphoreCount = array_count(signal_semaphores),
		.pSignalSemaphores = signal_semaphores
	};

	if (vkQueueSubmit(renderer->graphics_queue, 1, &submit_info, renderer->in_flight_fence) != VK_SUCCESS) {
		LOG_ERROR("Failed to submit draw command buffer");
		return;
	}

	VkSwapchainKHR swapchains[] = { renderer->swapchain };
	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = signal_semaphores,
		.swapchainCount = 1,
		.pSwapchains = swapchains,
		.pImageIndices = &image_index
	};

	if (vkQueuePresentKHR(renderer->present_queue, &present_info) != VK_SUCCESS) {
		return;
	}
}
