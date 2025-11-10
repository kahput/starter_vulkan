#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include "renderer/vk_renderer.h"

#include <string.h>
#include <vulkan/vulkan_core.h>

static const char *extensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
static char physical_device_name[256];

static bool vk_select_physical_device(VulkanState *vk_state);
static QueueFamilyIndices find_queue_families(VulkanState *vk_state);

bool vk_create_device(VulkanState *vk_state) {
	if (vk_select_physical_device(vk_state) == false)
		return false;

	vk_state->device.queue_indices = find_queue_families(vk_state);

	if (vk_state->device.queue_indices.graphics == -1 || vk_state->device.queue_indices.present == -1) {
		LOG_ERROR("Failed to find suitable GPU");
		return false;
	}

	LOG_INFO("Graphic and Present queues found");

	float queue_priority = 1.0f;

	uint32_t queue_families[] = { vk_state->device.queue_indices.graphics, vk_state->device.queue_indices.transfer, vk_state->device.queue_indices.present };
	uint32_t unique_count = 0;

	VkDeviceQueueCreateInfo queue_create_infos[array_count(queue_families)];
	for (uint32_t i = 0; i < array_count(queue_families); ++i) {
		bool duplicate = false;
		for (uint32_t j = 0; j < i; ++j) {
			if (queue_families[j] == queue_families[i]) {
				duplicate = true;
				break;
			}
		}
		if (duplicate)
			continue;
		queue_create_infos[unique_count++] = (VkDeviceQueueCreateInfo){
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = queue_families[i],
			.queueCount = 1,
			.pQueuePriorities = &queue_priority
		};
	}

	VkPhysicalDeviceFeatures features = { .samplerAnisotropy = true };

	VkDeviceCreateInfo device_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pQueueCreateInfos = queue_create_infos,
		.queueCreateInfoCount = unique_count,
		.pEnabledFeatures = &features,
		.ppEnabledExtensionNames = extensions,
		.enabledExtensionCount = sizeof(extensions) / sizeof(*extensions),
		// TODO: Set  this for compatibility
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = NULL
	};

	if (vkCreateDevice(vk_state->device.physical, &device_create_info, NULL, &vk_state->device.logical) != VK_SUCCESS) {
		LOG_ERROR("Failed to create logical device");
		return false;
	}

	vkGetDeviceQueue(vk_state->device.logical, vk_state->device.queue_indices.graphics, 0, &vk_state->device.graphics_queue);
	vkGetDeviceQueue(vk_state->device.logical, vk_state->device.queue_indices.transfer, 0, &vk_state->device.transfer_queue);
	vkGetDeviceQueue(vk_state->device.logical, vk_state->device.queue_indices.present, 0, &vk_state->device.present_queue);

	LOG_INFO("Logical device created");

	return true;
}

QueueFamilyIndices find_queue_families(VulkanState *vk_state) {
	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(vk_state->device.physical, &queue_family_count, NULL);

	ArenaTemp temp = arena_get_scratch(NULL);
	VkQueueFamilyProperties *queue_family_properties = arena_push_array_zero(temp.arena, VkQueueFamilyProperties, queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(vk_state->device.physical, &queue_family_count, queue_family_properties);

	QueueFamilyIndices family_indices = { -1, -1, -1 };
	for (uint32_t index = 0; index < queue_family_count; ++index) {
		VkQueueFlags flags = queue_family_properties[index].queueFlags;

		if ((flags & VK_QUEUE_GRAPHICS_BIT) && family_indices.graphics == -1)
			family_indices.graphics = index;

		if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_TRANSFER_BIT) && family_indices.transfer == -1)
			family_indices.transfer = index;

		VkBool32 present_support = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(vk_state->device.physical, index, vk_state->surface, &present_support);
		if (present_support && family_indices.present == -1)
			family_indices.present = index;
	}

	// LOG_INFO("QUEUE_FAMILY = { graphic_family = %d, transfer_family = %d, present_family = %d }", family_indices.graphics, family_indices.transfer, family_indices.present);

	arena_reset_scratch(temp);
	return family_indices;
}

static bool is_device_suitable(VkPhysicalDevice physical_device, VkSurfaceKHR surface, SwapChainSupportDetails *details);

bool vk_select_physical_device(VulkanState *vk_state) {
	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(vk_state->instance, &device_count, NULL);

	if (device_count == 0) {
		LOG_WARN("No physical devices found");
		return false;
	}

	ArenaTemp temp = arena_get_scratch(NULL);
	VkPhysicalDevice *physical_devices = arena_push_array_zero(temp.arena, VkPhysicalDevice, device_count);
	vkEnumeratePhysicalDevices(vk_state->instance, &device_count, physical_devices);

	bool found_suitable = false;
	for (uint32_t index = 0; index < device_count; index++) {
		if (is_device_suitable(physical_devices[index], vk_state->surface, &vk_state->device.details)) {
			vk_state->device.physical = physical_devices[index];
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

bool is_device_suitable(VkPhysicalDevice physical_device, VkSurfaceKHR surface, SwapChainSupportDetails *details) {
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

	if (extensions_supported) {
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &details->capabilities);

		vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &details->format_count, NULL);
		if (details->format_count == 0) {
			LOG_ERROR("No surface formats available");
			return false;
		}

		details->formats = arena_push_array_zero(temp.arena, VkSurfaceFormatKHR, details->format_count);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &details->format_count, details->formats);

		vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &details->present_mode_count, NULL);
		if (details->present_mode_count == 0) {
			LOG_ERROR("No surface modes available");
			arena_reset_scratch(temp);
			return false;
		}

		details->present_modes = arena_push_array_zero(temp.arena, VkPresentModeKHR, details->present_mode_count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &details->present_mode_count, details->present_modes);

		LOG_INFO("Swapchain available");
	}

	arena_reset_scratch(temp);

	return is_suitable;
}
