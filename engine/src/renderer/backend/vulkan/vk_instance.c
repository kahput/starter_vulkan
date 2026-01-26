#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "common.h"
#include "core/debug.h"
#include "core/logger.h"
#include "core/arena.h"

#include "platform.h"
#include <string.h>

static const char *layers[] = {
	"VK_LAYER_KHRONOS_validation",
};

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	void *pUserData);

VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info = {
	.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
	.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
	.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
	.pfnUserCallback = vulkan_debug_callback,
	.pUserData = NULL
};
void create_debug_messenger(VulkanContext *context);

bool vulkan_instance_create(VulkanContext *context, void *display) {
	VkApplicationInfo app_info = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Hello vulkan",
		.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
		.pEngineName = "No engine",
		.engineVersion = VK_MAKE_VERSION(1, 0, 0),
		.apiVersion = VK_API_VERSION_1_3
	};

	uint32_t platform_extension_count = 0, extension_count = 0;
	const char **extensions = platform_vulkan_extensions(display, &platform_extension_count);
	vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL);

	ArenaTemp scratch = arena_scratch(NULL);
	VkExtensionProperties *properties = arena_push_array_zero(scratch.arena, VkExtensionProperties, extension_count);
	vkEnumerateInstanceExtensionProperties(NULL, &extension_count, properties);

	for (uint32_t request_index = 0; request_index < platform_extension_count; ++request_index) {
		bool found = false;
		for (uint32_t extension_index = 0; extension_index < extension_count; ++extension_index) {
			if (strcmp(properties[extension_index].extensionName, extensions[request_index]) == 0)
				found = true;
		}
		if (found == false) {
			LOG_FATAL("Extension '%s' not found, aborting vulkan_instance_create", extensions[request_index]);
			ASSERT(false);
			arena_release_scratch(scratch);
			return false;
		}
	}

	VkInstanceCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_info,
		.enabledExtensionCount = platform_extension_count,
		.ppEnabledExtensionNames = extensions,
	};

#ifndef NDEBUG

	uint32_t requested_layers = sizeof(layers) / sizeof(*layers), available_layers = 0;
	vkEnumerateInstanceLayerProperties(&available_layers, NULL);

	VkLayerProperties *layer_properties = arena_push_array_zero(scratch.arena, VkLayerProperties, available_layers);
	vkEnumerateInstanceLayerProperties(&available_layers, layer_properties);

	uint32_t match = 0;
	for (uint32_t i = 0; i < available_layers; i++)
		for (uint32_t j = 0; j < requested_layers; j++)
			if (strcmp(layer_properties[i].layerName, layers[j]) == 0)
				match++;

	if (match != requested_layers) {
		LOG_ERROR("Requested layers not found");
		arena_release_scratch(scratch);
		return false;
	}
	create_info.enabledLayerCount = requested_layers;
	create_info.ppEnabledLayerNames = layers;

	const char *debug_extensions[platform_extension_count + 1];
	for (uint32_t i = 0; i < platform_extension_count; i++) {
		debug_extensions[i] = extensions[i];
	}

	debug_extensions[platform_extension_count] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

	create_info.enabledExtensionCount = sizeof(debug_extensions) / sizeof(*debug_extensions);
	create_info.ppEnabledExtensionNames = debug_extensions;

	// VkValidationFeaturesEXT features = {
	// 	.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT
	// };
	//
	// VkValidationFeatureEnableEXT enables[] = {
	// 	VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
	// 	VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT
	// };
	//
	// features.enabledValidationFeatureCount = 2;
	// features.pEnabledValidationFeatures = enables;
	// debug_utils_create_info.pNext = &features;

	create_info.pNext = &debug_utils_create_info;
#endif

	VkResult result = vkCreateInstance(&create_info, NULL, &context->instance);
	if (result != VK_SUCCESS) {
		LOG_ERROR("Vulkan instance creation failed");
		arena_release_scratch(scratch);
		return false;
	}

	LOG_INFO("Vulkan Instance created");

	arena_release_scratch(scratch);
	return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
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

void create_debug_messenger(VulkanContext *context) {
	vkCreateDebugUtilsMessenger(context->instance, &debug_utils_create_info, NULL, &context->debug_messenger);
}

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
