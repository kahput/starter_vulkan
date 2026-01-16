#include "core/debug.h"
#include "renderer/backend/vulkan_api.h"

#include "vk_internal.h"

#include "core/arena.h"
#include "common.h"
#include "core/logger.h"

#include <string.h>
#include <vulkan/vulkan_core.h>

static const char *extensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static bool select_physical_device(Arena *arena, VulkanContext *context);
static bool is_device_suitable(Arena *arena, VkPhysicalDevice physical_device, VkSurfaceKHR surface, VulkanDevice *device);
static VkFormat find_supported_depth_format(VulkanDevice *device);

bool vulkan_device_create(Arena *arena, VulkanContext *context) {
	if (select_physical_device(arena, context) == false)
		return false;

	float queue_priority = 1.0f;
	uint32_t queue_families[] = { context->device.graphics_index, context->device.transfer_index, context->device.present_index };
	uint32_t unique_count = 0;

	VkDeviceQueueCreateInfo queue_create_infos[countof(queue_families)];
	for (uint32_t i = 0; i < countof(queue_families); ++i) {
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

	VkPhysicalDeviceVulkan13Features vk13_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.dynamicRendering = true
	};
	VkPhysicalDeviceFeatures2 features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &vk13_features, .features = { .samplerAnisotropy = true, .fillModeNonSolid = true } };

	VkDeviceCreateInfo device_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &features,
		.pQueueCreateInfos = queue_create_infos,
		.queueCreateInfoCount = unique_count,
		.ppEnabledExtensionNames = extensions,
		.enabledExtensionCount = sizeof(extensions) / sizeof(*extensions),
		// TODO: Set  this for compatibility
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = NULL,
	};

	if (vkCreateDevice(context->device.physical, &device_create_info, NULL, &context->device.logical) != VK_SUCCESS) {
		LOG_ERROR("Failed to create logical device");
		return false;
	}

	vkGetDeviceQueue(context->device.logical, context->device.graphics_index, 0, &context->device.graphics_queue);
	vkGetDeviceQueue(context->device.logical, context->device.transfer_index, 0, &context->device.transfer_queue);
	vkGetDeviceQueue(context->device.logical, context->device.present_index, 0, &context->device.present_queue);

	context->device.depth_format = find_supported_depth_format(&context->device);
	context->device.sample_count = vulkan_utils_max_sample_count(context);
	context->device.multi_sample = context->device.sample_count > 1;

	LOG_INFO("Logical device created");

	return true;
}

bool select_physical_device(Arena *arena, VulkanContext *context) {
	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(context->instance, &device_count, NULL);

	if (device_count == 0) {
		LOG_WARN("Failed to find GPUs with Vulkan support");
		return false;
	}

	ArenaTemp scratch = arena_scratch(NULL);
	VkPhysicalDevice *physical_devices = arena_push_array_zero(scratch.arena, VkPhysicalDevice, device_count);
	vkEnumeratePhysicalDevices(context->instance, &device_count, physical_devices);

	LOG_INFO("Selecting suitable device...");
	bool found_suitable = false;
	for (uint32_t index = 0; index < device_count; index++) {
		if (is_device_suitable(arena, physical_devices[index], context->surface, &context->device)) {
			context->device.physical = physical_devices[index];
			found_suitable = true;
			break;
		}
	}

	arena_release_scratch(scratch);
	return found_suitable;
}

bool is_device_suitable(Arena *arena, VkPhysicalDevice physical_device, VkSurfaceKHR surface, VulkanDevice *device) {
	vkGetPhysicalDeviceProperties(physical_device, &device->properties);
	vkGetPhysicalDeviceFeatures(physical_device, &device->features);

	if (device->properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		return false;

	if (!(device->features.geometryShader && device->features.samplerAnisotropy))
		return false;

	ArenaTemp scratch = arena_scratch(NULL);
	uint32_t requested_extensions = countof(extensions), available_extensions = 0;
	vkEnumerateDeviceExtensionProperties(physical_device, NULL, &available_extensions, NULL);

	VkExtensionProperties *properties = arena_push_array_zero(scratch.arena, VkExtensionProperties, available_extensions);
	vkEnumerateDeviceExtensionProperties(physical_device, NULL, &available_extensions, properties);

	for (uint32_t i = 0; i < requested_extensions; i++) {
		bool found = false;
		for (uint32_t j = 0; j < available_extensions; j++) {
			if (strcmp(extensions[i], properties[j].extensionName) == 0) {
				found = true;
				break;
			}
		}
		if (found == false) {
			LOG_ERROR("Required device extension [ %s ] not found");
			return false;
		}
	}

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &device->swapchain_details.capabilities);

	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &device->swapchain_details.format_count, NULL);
	if (device->swapchain_details.format_count == 0) {
		LOG_ERROR("No surface formats available");
		arena_release_scratch(scratch);
		return false;
	}

	device->swapchain_details.formats = arena_push_array_zero(arena, VkSurfaceFormatKHR, device->swapchain_details.format_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &device->swapchain_details.format_count, device->swapchain_details.formats);

	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &device->swapchain_details.present_mode_count, NULL);
	if (device->swapchain_details.present_mode_count == 0) {
		LOG_ERROR("No surface modes available");
		arena_release_scratch(scratch);
		return false;
	}

	device->swapchain_details.present_modes = arena_push_array_zero(arena, VkPresentModeKHR, device->swapchain_details.present_mode_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &device->swapchain_details.present_mode_count, device->swapchain_details.present_modes);

	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, NULL);

	VkQueueFamilyProperties *queue_family_properties = arena_push_array_zero(scratch.arena, VkQueueFamilyProperties, queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_family_properties);

	device->graphics_index = -1, device->transfer_index = -1,
	device->present_index = -1;
	for (uint32_t index = 0; index < queue_family_count; ++index) {
		VkQueueFlags flags = queue_family_properties[index].queueFlags;

		if ((flags & VK_QUEUE_GRAPHICS_BIT) && device->graphics_index == -1)
			device->graphics_index = index;

		if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_TRANSFER_BIT) && device->transfer_index == -1)
			device->transfer_index = index;

		VkBool32 present_support = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, surface, &present_support);
		if (present_support && device->present_index == -1)
			device->present_index = index;
	}

	arena_release_scratch(scratch);
	LOG_INFO("Device '%s' selected", device->properties.deviceName);

	return true;
}
VkFormat find_supported_depth_format(VulkanDevice *device) {
	VkFormat options[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
	VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	VkFormatFeatureFlags feature = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

	for (uint32_t format_index = 0; format_index < countof(options); ++format_index) {
		VkFormat format = options[format_index];
		VkFormatProperties properties;
		vkGetPhysicalDeviceFormatProperties(device->physical, format, &properties);

		if (FLAG_GET(properties.optimalTilingFeatures, feature)) {
			return format;
		}
	}

	ASSERT(false);
}
