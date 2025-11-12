#include "platform.h"

#include "core/logger.h"
#include "renderer/vk_renderer.h"

bool vk_create_surface(Platform *platform, VulkanContext *context) {
	if (platform_create_vulkan_surface(platform, context->instance, &context->surface) == false) {
		LOG_ERROR("Failed to create Vulkan surface");
		return false;
	}

	LOG_INFO("Vulkan Surface created");
	return true;
}
