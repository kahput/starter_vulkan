#include "core/logger.h"
#include "vk_renderer.h"

#include "platform.h"

bool vk_create_surface( Platform *platform, VulkanRenderer *renderer) {
	VkXcbSurfaceCreateInfoKHR surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
		.window = *(xcb_window_t *)platform_window_handle(platform),
		.connection = (xcb_connection_t *)platform_instance_handle(platform),
	};

	if (vkCreateXcbSurfaceKHR(renderer->instance, &surface_create_info, NULL, &renderer->surface) != VK_SUCCESS) {
		LOG_ERROR("Failed to create xcb surface");
		return false;
	}

	return true;
}
