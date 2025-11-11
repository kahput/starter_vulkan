#include "renderer/vk_renderer.h"

#include "platform.h"

#include "core/arena.h"
#include "core/logger.h"

#include <string.h>

static const char *layers[] = {
	"VK_LAYER_KHRONOS_validation",
};

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	void *pUserData);

VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info = {
	.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
	.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
	.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
	.pfnUserCallback = vk_debug_callback,
	.pUserData = NULL
};
void create_debug_messenger(VulkanContext *ctx);

bool vk_create_instance(VulkanContext *ctx, Platform *platform) {
	VkApplicationInfo app_info = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Hello vulkan",
		.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
		.pEngineName = "No engine",
		.engineVersion = VK_MAKE_VERSION(1, 0, 0),
		.apiVersion = VK_API_VERSION_1_4
	};

	uint32_t requested_extensions = 0, available_extensions = 0;
	const char **extensions = platform_vulkan_extensions(platform, &requested_extensions);
	vkEnumerateInstanceExtensionProperties(NULL, &available_extensions, NULL);

	ArenaTemp temp = arena_get_scratch(NULL);
	VkExtensionProperties *properties = arena_push_array_zero(temp.arena, VkExtensionProperties, available_extensions);
	vkEnumerateInstanceExtensionProperties(NULL, &available_extensions, properties);

	uint32_t match = 0;
	for (uint32_t i = 0; i < available_extensions; i++) {
		for (uint32_t j = 0; j < requested_extensions; j++) {
			if (strcmp(properties[i].extensionName, extensions[j]) == 0)
				match++;
		}
	}
	if (match != requested_extensions) {
		LOG_ERROR("Requested extensions not found");
		return false;
	}

	VkInstanceCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_info,
		.enabledExtensionCount = requested_extensions,
		.ppEnabledExtensionNames = extensions,
	};

#ifndef NDEBUG
	uint32_t requested_layers = sizeof(layers) / sizeof(*layers), available_layers = 0;
	vkEnumerateInstanceLayerProperties(&available_layers, NULL);

	VkLayerProperties *layer_properties = arena_push_array_zero(temp.arena, VkLayerProperties, available_layers);
	vkEnumerateInstanceLayerProperties(&available_layers, layer_properties);

	match = 0;
	for (uint32_t i = 0; i < available_layers; i++)
		for (uint32_t j = 0; j < requested_layers; j++)
			if (strcmp(layer_properties[i].layerName, layers[j]) == 0)
				match++;

	if (match != requested_layers) {
		LOG_ERROR("Requested layers not found");
		return false;
	}
	create_info.enabledLayerCount = requested_layers;
	create_info.ppEnabledLayerNames = layers;

	const char *debug_extensions[requested_extensions + 1];
	for (uint32_t i = 0; i < requested_extensions; i++) {
		debug_extensions[i] = extensions[i];
	}

	debug_extensions[requested_extensions] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

	create_info.enabledExtensionCount = sizeof(debug_extensions) / sizeof(*debug_extensions);
	create_info.ppEnabledExtensionNames = debug_extensions;

	create_info.pNext = &debug_utils_create_info;
#endif

	VkResult result = vkCreateInstance(&create_info, NULL, &ctx->instance);
	if (result != VK_SUCCESS) {
		LOG_ERROR("Vulkan instance creation failed");
		return false;
	}

	LOG_INFO("Vulkan Instance created");

	arena_reset_scratch(temp);

	return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
	VkDebugUtilsMessageTypeFlagsEXT message_type,
	const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
	void *pUserData) {
	switch (message_severity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: {
			LOG_TRACE("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: {
			LOG_INFO("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
			LOG_WARN("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
			LOG_ERROR("%s", callback_data->pMessage);
			return VK_FALSE;
		} break;
		default: {
			return VK_FALSE;
		} break;
	}
}

void create_debug_messenger(VulkanContext *ctx) {
	vkCreateDebugUtilsMessenger(ctx->instance, &debug_utils_create_info, NULL, &ctx->debug_messenger);
}
