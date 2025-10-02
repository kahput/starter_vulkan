#include "vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"

#include <string.h>
#include <vulkan/vulkan_core.h>

static const char *extensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static bool create_logical_device(VKRenderer *renderer, QueueFamilyIndices *indices);

bool vk_create_logical_device(Arena *arena, VKRenderer *renderer) {
	QueueFamilyIndices family_indices = find_queue_families(arena, renderer);

	if (family_indices.graphics == -1 || family_indices.present == -1) {
		LOG_ERROR("Failed to find suitable GPU");
		arena_clear(arena);
		return false;
	}

	LOG_INFO("Graphic and Present queues found");

	if (create_logical_device(renderer, &family_indices) == false) {
		LOG_ERROR("Failed to create logical device");
		arena_clear(arena);
		return false;
	}

	LOG_INFO("Logical device created");

	arena_clear(arena);
	return true;
}

QueueFamilyIndices find_queue_families(Arena *scratch_arena, VKRenderer *renderer) {
	uint32_t offset = arena_size(scratch_arena);

	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device, &queue_family_count, NULL);

	VkQueueFamilyProperties *queue_family_properties = arena_push_array_zero(scratch_arena, VkQueueFamilyProperties, queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device, &queue_family_count, queue_family_properties);

	QueueFamilyIndices family_indices = { -1, -1, -1 };
	for (uint32_t index = 0; index < queue_family_count; ++index) {
		VkQueueFlags flags = queue_family_properties[index].queueFlags;

		if ((flags & VK_QUEUE_GRAPHICS_BIT) && family_indices.graphics == -1)
			family_indices.graphics = index;

		if ((flags & VK_QUEUE_GRAPHICS_BIT) == false && (flags & VK_QUEUE_TRANSFER_BIT) && family_indices.transfer == -1)
			family_indices.transfer = index;

		VkBool32 present_support = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(renderer->physical_device, index, renderer->surface, &present_support);
		if (present_support && family_indices.present == -1)
			family_indices.present = index;
	}

	// LOG_INFO("QUEUE_FAMILY = { graphic_family = %d, transfer_family = %d, present_family = %d }", family_indices.graphics, family_indices.transfer, family_indices.present);

	arena_set(scratch_arena, offset);
	return family_indices;
}

bool create_logical_device(VKRenderer *renderer, QueueFamilyIndices *family_indices) {
	float queue_priority = 1.0f;

	uint32_t queue_families[] = { family_indices->graphics, family_indices->transfer, family_indices->present };
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

	VkPhysicalDeviceFeatures features = { 0 };

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

	if (vkCreateDevice(renderer->physical_device, &device_create_info, NULL, &renderer->logical_device) != VK_SUCCESS)
		return false;

	vkGetDeviceQueue(renderer->logical_device, family_indices->graphics, 0, &renderer->graphics_queue);
	vkGetDeviceQueue(renderer->logical_device, family_indices->transfer, 0, &renderer->transfer_queue);
	vkGetDeviceQueue(renderer->logical_device, family_indices->present, 0, &renderer->present_queue);

	return true;
}
