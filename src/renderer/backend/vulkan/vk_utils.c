#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"

VkSampleCountFlags vulkan_utils_max_sample_count(VulkanContext *context) {
	VkPhysicalDeviceLimits limits = context->device.properties.limits;

	VkSampleCountFlags flags = limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts;

	if (flags & VK_SAMPLE_COUNT_64_BIT)
		return VK_SAMPLE_COUNT_64_BIT;
	if (flags & VK_SAMPLE_COUNT_32_BIT)
		return VK_SAMPLE_COUNT_32_BIT;
	if (flags & VK_SAMPLE_COUNT_16_BIT)
		return VK_SAMPLE_COUNT_16_BIT;
	if (flags & VK_SAMPLE_COUNT_8_BIT)
		return VK_SAMPLE_COUNT_8_BIT;
	if (flags & VK_SAMPLE_COUNT_4_BIT)
		return VK_SAMPLE_COUNT_4_BIT;
	if (flags & VK_SAMPLE_COUNT_2_BIT)
		return VK_SAMPLE_COUNT_2_BIT;

	return VK_SAMPLE_COUNT_1_BIT;
}
