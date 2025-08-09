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

static QueueFamilyIndices find_queue_families(Arena *arena, VulkanRenderer *renderer);
static bool create_logical_device(VulkanRenderer *renderer, QueueFamilyIndices *indices);

bool vk_create_logical_device(Arena *arena, VulkanRenderer *renderer) {
	QueueFamilyIndices indices = find_queue_families(arena, renderer);

	if (indices.graphic_family == -1 || indices.present_family == -1) {
		LOG_ERROR("Failed to find suitable GPU");
		arena_clear(arena);
		return false;
	}

	LOG_INFO("Graphic and Present queues found");

	if (create_logical_device(renderer, &indices) == false) {
		LOG_ERROR("Failed to create logical device");
		arena_clear(arena);
		return false;
	}

	LOG_INFO("Logical device created");

	arena_clear(arena);
	return true;
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
