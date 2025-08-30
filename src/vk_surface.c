#include "platform.h"

#include "core/logger.h"
#include "vk_renderer.h"

bool vk_create_surface(Platform *platform, VKRenderer *renderer) {
	if (platform_create_vulkan_surface(platform, renderer->instance, &renderer->surface) == false) {
		LOG_ERROR("Failed to create Vulkan surface");
		return false;
	}

	LOG_INFO("Vulkan surface created successfully");
	return true;
}
