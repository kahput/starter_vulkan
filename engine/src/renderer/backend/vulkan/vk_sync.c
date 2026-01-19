#include "renderer/backend/vulkan_api.h"

#include "vk_internal.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vulkan_sync_objects_create(VulkanContext *context) {
	VkSemaphoreCreateInfo s_create_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	VkFenceCreateInfo f_create_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};

	for (uint32_t frame_index = 0; frame_index < MAX_FRAMES_IN_FLIGHT; ++frame_index) {
		if (vkCreateSemaphore(context->device.logical, &s_create_info, NULL, context->image_available_semaphores + frame_index) != VK_SUCCESS ||
			vkCreateFence(context->device.logical, &f_create_info, NULL, context->in_flight_fences + frame_index) != VK_SUCCESS) {
			LOG_ERROR("Failed to create synchronization objects");
			return false;
		}
	}

	for (uint32_t image_index = 0; image_index < SWAPCHAIN_IMAGE_COUNT; ++image_index) {
		if (vkCreateSemaphore(context->device.logical, &s_create_info, NULL, context->render_finished_semaphores + image_index) != VK_SUCCESS) {
			LOG_ERROR("Failed to create synchronization objects");
			return false;
		}
	}

	LOG_INFO("Vulkan Synchronization Objects created");

	return true;
}
