#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_sync_objects(VulkanState *vk_state) {
	VkSemaphoreCreateInfo s_create_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	VkFenceCreateInfo f_create_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		if (vkCreateSemaphore(vk_state->device.logical, &s_create_info, NULL, vk_state->image_available_semaphores + i) != VK_SUCCESS ||
			vkCreateSemaphore(vk_state->device.logical, &s_create_info, NULL, vk_state->render_finished_semaphores + i) != VK_SUCCESS ||
			vkCreateFence(vk_state->device.logical, &f_create_info, NULL, vk_state->in_flight_fences + i) != VK_SUCCESS) {
			LOG_ERROR("Failed to create synchronization objects");
			return false;
		}
	}

	LOG_INFO("Vulkan Synchronization Objects created");

	return true;
}
