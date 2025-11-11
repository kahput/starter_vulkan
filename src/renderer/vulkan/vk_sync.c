#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_sync_objects(VulkanContext *ctx) {
	VkSemaphoreCreateInfo s_create_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	VkFenceCreateInfo f_create_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		if (vkCreateSemaphore(ctx->device.logical, &s_create_info, NULL, ctx->image_available_semaphores + i) != VK_SUCCESS ||
			vkCreateSemaphore(ctx->device.logical, &s_create_info, NULL, ctx->render_finished_semaphores + i) != VK_SUCCESS ||
			vkCreateFence(ctx->device.logical, &f_create_info, NULL, ctx->in_flight_fences + i) != VK_SUCCESS) {
			LOG_ERROR("Failed to create synchronization objects");
			return false;
		}
	}

	LOG_INFO("Vulkan Synchronization Objects created");

	return true;
}
