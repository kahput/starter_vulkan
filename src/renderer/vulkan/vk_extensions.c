#include "core/logger.h"
#include "renderer/vk_renderer.h"

#include <vulkan/vulkan.h>
VK_CREATE_UTIL_DEBUG_MESSENGER(vk_create_utils_debug_messneger_default) {
	return VK_ERROR_EXTENSION_NOT_PRESENT;
}
void vk_load_extensions(VulkanContext *ctx) {
	vkCreateDebugUtilsMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(ctx->instance, "vkCreateDebugUtilsMessengerEXT");
	if (vkCreateDebugUtilsMessenger == NULL)
		LOG_ERROR("Failed to load vkCreateDebugUtilsMessenger extension");
}
