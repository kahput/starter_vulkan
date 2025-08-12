#include "platform.h"

#include "core/logger.h"
#include "vk_renderer.h"

bool vk_create_surface(Platform *platform, VulkanRenderer *renderer) {
	if (platform_create_vulkan_surface(platform, renderer->instance, &renderer->surface) == false) {
		LOG_ERROR("Failed to create xcb surface");
		return false;
	}

	LOG_INFO("Vulkan XCB surface created");
	return true;
}
