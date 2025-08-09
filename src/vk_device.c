#include "vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <string.h>
#include <vulkan/vulkan_core.h>

static const char *extensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

typedef struct {
	int32_t graphic_family;
	int32_t present_family;
} QueueFamilyIndices;

static bool select_physical_device(Arena *arena, VulkanRenderer *renderer);
static QueueFamilyIndices find_queue_families(Arena *arena, VulkanRenderer *renderer);
static bool create_logical_device(VulkanRenderer *renderer, QueueFamilyIndices *indices);

bool vk_create_device(Arena *arena, VulkanRenderer *renderer) {
	if (select_physical_device(arena, renderer) == false) {
		LOG_ERROR("Failed to find suitable GPU");
		arena_clear(arena);
		return false;
	}

	QueueFamilyIndices indices = find_queue_families(arena, renderer);

	if (indices.graphic_family == -1 || indices.present_family == -1) {
		LOG_ERROR("Failed to find suitable GPU");
		arena_clear(arena);
		return false;
	}

	if (create_logical_device(renderer, &indices) == false) {
		LOG_ERROR("Failed to create logical device");
		arena_clear(arena);
		return false;
	}

	arena_clear(arena);
	return true;
}

bool is_device_suitable(Arena *arena, VkPhysicalDevice device) {
	VkPhysicalDeviceProperties device_properties = { 0 };
	vkGetPhysicalDeviceProperties(device, &device_properties);

	bool is_suitable = device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
	LOG_INFO("Card: %s", device_properties.deviceName);

	VkPhysicalDeviceFeatures device_features = { 0 };
	vkGetPhysicalDeviceFeatures(device, &device_features);

	is_suitable = is_suitable && device_features.geometryShader;

	uint32_t requested_extensions = sizeof(extensions) / sizeof(*extensions), available_extensions = 0;
	vkEnumerateDeviceExtensionProperties(device, NULL, &available_extensions, NULL);

	VkExtensionProperties *properties = arena_push_array_zero(arena, VkExtensionProperties, available_extensions);
	vkEnumerateDeviceExtensionProperties(device, NULL, &available_extensions, properties);

	uint32_t extensions_available = 0;
	for (uint32_t i = 0; i < available_extensions; i++) {
		for (uint32_t j = 0; j < requested_extensions; j++)
			if (strcmp(properties[i].extensionName, extensions[j]) == 0) {
				extensions_available++;
			}
	}

	is_suitable = is_suitable && (extensions_available >= requested_extensions);

	return is_suitable;
}

bool select_physical_device(Arena *arena, VulkanRenderer *renderer) {
	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(renderer->instance, &device_count, NULL);

	if (device_count == 0) {
		LOG_WARN("No physical devices found");
		return false;
	}

	VkPhysicalDevice *physical_devices = arena_push_array_zero(arena, VkPhysicalDevice, device_count);
	vkEnumeratePhysicalDevices(renderer->instance, &device_count, physical_devices);

	bool found_suitable = false;
	for (uint32_t index = 0; index < device_count; index++) {
		if (is_device_suitable(arena, physical_devices[index])) {
			renderer->physical_device = physical_devices[index];
			found_suitable = true;
			break;
		}
	}

	return found_suitable;
}

QueueFamilyIndices find_queue_families(Arena *arena, VulkanRenderer *renderer) {
	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device, &queue_family_count, NULL);

	VkQueueFamilyProperties *queue_family_properties = arena_push_array_zero(arena, VkQueueFamilyProperties, queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device, &queue_family_count, queue_family_properties);

	QueueFamilyIndices indices = { -1, -1 };
	for (uint32_t index = 0; index < queue_family_count; ++index) {
		if (queue_family_properties->queueFlags & VK_QUEUE_GRAPHICS_BIT)
			indices.graphic_family = index;

		VkBool32 present_support = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(renderer->physical_device, index, renderer->surface, &present_support);
		if (present_support)
			indices.present_family = index;
	}

	return indices;
}

bool create_logical_device(VulkanRenderer *renderer, QueueFamilyIndices *indices) {
	float queue_priority = 1.0f;

	uint32_t unique_queue_families[2] = { indices->graphic_family, indices->present_family };
	uint32_t queue_family_count = indices->graphic_family == indices->present_family ? 1 : 2;

	VkDeviceQueueCreateInfo queue_create_infos[2];
	for (uint32_t i = 0; i < queue_family_count; i++) {
		queue_create_infos[i] = (VkDeviceQueueCreateInfo){
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = unique_queue_families[i],
			.queueCount = 1,
			.pQueuePriorities = &queue_priority
		};
	}

	VkPhysicalDeviceFeatures features = { 0 };

	VkDeviceCreateInfo device_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pQueueCreateInfos = queue_create_infos,
		.queueCreateInfoCount = queue_family_count,
		.pEnabledFeatures = &features,
		.ppEnabledExtensionNames = extensions,
		.enabledExtensionCount = sizeof(extensions) / sizeof(*extensions),
		// TODO: Set  this for compatibility
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = NULL
	};

	if (vkCreateDevice(renderer->physical_device, &device_create_info, NULL, &renderer->logical_device) != VK_SUCCESS)
		return false;

	vkGetDeviceQueue(renderer->logical_device, indices->graphic_family, 0, &renderer->graphics_queue);
	vkGetDeviceQueue(renderer->logical_device, indices->present_family, 0, &renderer->present_queue);

	return true;
}
