#include "core/logger.h"
#include "renderer/vk_renderer.h"

#include <vulkan/vulkan.h>
VK_CREATE_UTIL_DEBUG_MESSENGER(vulkan_create_utils_debug_messneger_default) {
	return VK_ERROR_EXTENSION_NOT_PRESENT;
}
void vulkan_load_extensions(VulkanContext *context) {
	vkCreateDebugUtilsMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance, "vkCreateDebugUtilsMessengerEXT");
	if (vkCreateDebugUtilsMessenger == NULL)
		LOG_ERROR("Failed to load vkCreateDebugUtilsMessenger extension");
}
