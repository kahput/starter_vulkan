#include "core/logger.h"
#include "renderer/vk_renderer.h"

#include "vk_internal.h"

#include <vulkan/vulkan.h>
VK_CREATE_UTIL_DEBUG_MESSENGER(vulkan_create_utils_debug_messneger_default) {
	return VK_ERROR_EXTENSION_NOT_PRESENT;
}
VK_DESTROY_UTIL_DEBUG_MESSENGER(vulkan_destroy_utils_debug_messneger_default) {
	return;
}

void vulkan_load_extensions(VulkanContext *context) {
	vkCreateDebugUtilsMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance, "vkCreateDebugUtilsMessengerEXT");
	vkDestroyDebugUtilsMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context->instance, "vkDestroyDebugUtilsMessenger");
	if (vkCreateDebugUtilsMessenger == NULL)
		LOG_ERROR("Failed to load vkCreateDebugUtilsMessenger extension");
	if (vkDestroyDebugUtilsMessenger == NULL)
		LOG_ERROR("Failed to load vkDestroyDebugUtilsMessenger extension");
}
