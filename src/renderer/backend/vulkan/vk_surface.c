#include "platform.h"

#include "vk_internal.h"

#include "core/logger.h"
#include "renderer/backend/vulkan_api.h"

bool vulkan_surface_create(VulkanContext *context, void *display) {
	if (platform_create_vulkan_surface(display, context->instance, &context->surface) == false) {
		LOG_ERROR("Failed to create Vulkan surface");
		return false;
	}

	LOG_INFO("Vulkan Surface created");
	return true;
}
