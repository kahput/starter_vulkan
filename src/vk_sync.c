#include "vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_sync_objects(VKRenderer *renderer) {
	VkSemaphoreCreateInfo s_create_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	VkFenceCreateInfo f_create_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	};

	if (vkCreateSemaphore(renderer->logical_device, &s_create_info, NULL, &renderer->image_available_semaphore) != VK_SUCCESS ||
		vkCreateSemaphore(renderer->logical_device, &s_create_info, NULL, &renderer->render_finished_semaphore) != VK_SUCCESS ||
		vkCreateFence(renderer->logical_device, &f_create_info, NULL, &renderer->in_flight_fence) != VK_SUCCESS) {
		LOG_ERROR("Failed to create synchronization objects");
		return false;
	}

	LOG_INFO("Vulkan Synchronization Ojbects Created Successfully");

	return true;
}
