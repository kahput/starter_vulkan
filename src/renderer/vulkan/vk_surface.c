#include "platform.h"

#include "core/logger.h"
#include "renderer/vk_renderer.h"

bool vk_create_surface(Platform *platform, VulkanState *vk_state) {
	if (platform_create_vulkan_surface(platform, vk_state->instance, &vk_state->surface) == false) {
		LOG_ERROR("Failed to create Vulkan surface");
		return false;
	}

	LOG_INFO("Vulkan Surface created");
	return true;
}
