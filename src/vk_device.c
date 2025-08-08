#include "core/arena.h"
#include "core/logger.h"
#include "vk_renderer.h"
#include <vulkan/vulkan_core.h>

static bool is_device_suitable(VkPhysicalDevice device);

bool vk_select_physical_device(Arena *arena, VulkanRenderer *renderer) {
	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(renderer->instance, &device_count, NULL);

	if (device_count == 0) {
		LOG_WARN("No physical devices found");
		return false;
	}

	VkPhysicalDevice *physical_devices = arena_push_array_zero(arena, VkPhysicalDevice, device_count);
	vkEnumeratePhysicalDevices(renderer->instance, &device_count, physical_devices);

	for (uint32_t index = 0; index < device_count; index++) {
		if (is_device_suitable(physical_devices[index])) {
			renderer->physical_device = physical_devices[index];
			break;
		}
	}
	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device, &queue_family_count, NULL);

	VkQueueFamilyProperties *queue_family_properties = arena_push_array_zero(arena, VkQueueFamilyProperties, queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device, &queue_family_count, queue_family_properties);

	int32_t graphic_queue_index = -1;
	for (uint32_t index = 0; index < queue_family_count; ++index) {
		if (queue_family_properties->queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			graphic_queue_index = index;
			break;
		}
	}
	if (graphic_queue_index == -1) {
		LOG_ERROR("Failed to find suitable GPU");
		arena_clear(arena);
		return false;
	}

	float queue_priortiy = 1.0f;
	VkDeviceQueueCreateInfo queue_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueCount = 1,
		.queueFamilyIndex = (uint32_t)graphic_queue_index,
		.pQueuePriorities = &queue_priortiy,

	};
	VkPhysicalDeviceFeatures features = { 0 };

	VkDeviceCreateInfo device_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pQueueCreateInfos = &queue_create_info,
		.queueCreateInfoCount = 1,
		.pEnabledFeatures = &features,
		// TODO: Set  this for compatibility
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = NULL
	};

	if (vkCreateDevice(renderer->physical_device, &device_create_info, NULL, &renderer->logical_device) != VK_SUCCESS) {
		LOG_ERROR("Failed to create logical device");
		arena_clear(arena);
		return false;
	}

	vkGetDeviceQueue(renderer->logical_device, graphic_queue_index, 0, &renderer->graphics_queue);

	arena_clear(arena);
	return false;
}

bool is_device_suitable(VkPhysicalDevice device) {
	VkPhysicalDeviceProperties device_properties = { 0 };
	vkGetPhysicalDeviceProperties(device, &device_properties);

	VkPhysicalDeviceFeatures device_features = { 0 };
	vkGetPhysicalDeviceFeatures(device, &device_features);

	LOG_INFO("Card: %s", device_properties.deviceName);

	return device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && device_features.geometryShader;
}
