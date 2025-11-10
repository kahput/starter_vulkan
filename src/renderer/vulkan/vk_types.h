#pragma once

#include "common.h"

#include <vulkan/vulkan_core.h>

typedef struct {
	int32_t graphics;
	int32_t transfer;
	int32_t present;
} QueueFamilyIndices;

typedef struct swapchain_support_details {
	VkSurfaceCapabilitiesKHR capabilities;

	VkSurfaceFormatKHR *formats;
	uint32_t format_count;

	VkPresentModeKHR *present_modes;
	uint32_t present_mode_count;
} SwapChainSupportDetails;

typedef struct vulkan_device {
	VkPhysicalDevice physical;
	VkDevice logical;
	SwapChainSupportDetails details;

	QueueFamilyIndices queue_indices;

	VkQueue graphics_queue, transfer_queue, present_queue;
} VulkanDevice;
