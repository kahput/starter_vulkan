#include "vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"

#include <stdio.h>
#include <string.h>

static const char *extensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static char physical_device_name[256];

bool is_device_suitable(VkPhysicalDevice physical_device, VkSurfaceKHR surface);

bool vk_select_physical_device(VKRenderer *renderer) {
	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(renderer->instance, &device_count, NULL);

	if (device_count == 0) {
		LOG_WARN("No physical devices found");
		return false;
	}

	ArenaTemp temp = arena_get_scratch(NULL);
	VkPhysicalDevice *physical_devices = arena_push_array_zero(temp.arena, VkPhysicalDevice, device_count);
	vkEnumeratePhysicalDevices(renderer->instance, &device_count, physical_devices);

	bool found_suitable = false;
	for (uint32_t index = 0; index < device_count; index++) {
		if (is_device_suitable(physical_devices[index], renderer->surface)) {
			renderer->physical_device = physical_devices[index];
			found_suitable = true;
			break;
		}
	}

	if (found_suitable == false) {
		LOG_ERROR("Failed to find suitable GPU");
		arena_reset_scratch(temp);
	}

	arena_reset_scratch(temp);
	LOG_INFO("Physical Device: %s", physical_device_name);

	return found_suitable;
}

bool is_device_suitable(VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
	VkPhysicalDeviceProperties device_properties = { 0 };
	vkGetPhysicalDeviceProperties(physical_device, &device_properties);

	bool is_suitable = device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
	snprintf(physical_device_name, 256, "%s", device_properties.deviceName);

	VkPhysicalDeviceFeatures device_features = { 0 };
	vkGetPhysicalDeviceFeatures(physical_device, &device_features);

	is_suitable = is_suitable && device_features.geometryShader && device_features.samplerAnisotropy;

	uint32_t requested_extensions = sizeof(extensions) / sizeof(*extensions), available_extensions = 0;
	vkEnumerateDeviceExtensionProperties(physical_device, NULL, &available_extensions, NULL);

	ArenaTemp temp = arena_get_scratch(NULL);
	VkExtensionProperties *properties = arena_push_array_zero(temp.arena, VkExtensionProperties, available_extensions);
	vkEnumerateDeviceExtensionProperties(physical_device, NULL, &available_extensions, properties);

	uint32_t extensions_available = 0;
	for (uint32_t i = 0; i < available_extensions; i++) {
		for (uint32_t j = 0; j < requested_extensions; j++)
			if (strcmp(properties[i].extensionName, extensions[j]) == 0) {
				extensions_available++;
			}
	}

	bool extensions_supported = extensions_available >= requested_extensions;
	is_suitable = is_suitable && extensions_supported;

	if (extensions_supported)
		is_suitable = is_suitable && query_swapchain_support(physical_device, surface);

	return is_suitable;
}
