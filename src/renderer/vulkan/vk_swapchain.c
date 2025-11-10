#include "platform.h"
#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"

#include "common.h"
#include <vulkan/vulkan_core.h>

VkSurfaceFormatKHR swapchain_select_surface_format(VkSurfaceFormatKHR *formats, uint32_t count);
VkPresentModeKHR swapchain_select_present_mode(VkPresentModeKHR *modes, uint32_t count);
VkExtent2D swapchain_select_extent(struct platform *platform, const VkSurfaceCapabilitiesKHR *capabilities);

bool vk_create_swapchain(VulkanState *vk_state, struct platform *platform) {
	vk_state->swapchain.format = swapchain_select_surface_format(vk_state->device.details.formats, vk_state->device.details.format_count);
	vk_state->swapchain.present_mode = swapchain_select_present_mode(vk_state->device.details.present_modes, vk_state->device.details.present_mode_count);
	vk_state->swapchain.extent = swapchain_select_extent(platform, &vk_state->device.details.capabilities);

	uint32_t queue_family_indices[] = { vk_state->device.queue_indices.graphics, vk_state->device.queue_indices.present };

	uint32_t image_count = vk_state->device.details.capabilities.minImageCount + 1;
	if (vk_state->device.details.capabilities.maxImageCount > 0 && image_count > vk_state->device.details.capabilities.maxImageCount)
		image_count = vk_state->device.details.capabilities.maxImageCount;

	VkSwapchainCreateInfoKHR swapchain_create_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = vk_state->surface,
		.minImageCount = image_count,
		.imageFormat = vk_state->swapchain.format.format,
		.imageColorSpace = vk_state->swapchain.format.colorSpace,
		.imageExtent = vk_state->swapchain.extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = vk_state->device.details.capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = vk_state->swapchain.present_mode,
		.clipped = VK_TRUE,
	};
	if (queue_family_indices[0] != queue_family_indices[1]) {
		swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapchain_create_info.queueFamilyIndexCount = 2;
		swapchain_create_info.pQueueFamilyIndices = queue_family_indices;
	} else
		swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateSwapchainKHR(vk_state->device.logical, &swapchain_create_info, NULL, &vk_state->swapchain.handle) != VK_SUCCESS) {
		LOG_ERROR("Failed to create swapchain");
		return false;
	}

	vkGetSwapchainImagesKHR(vk_state->device.logical, vk_state->swapchain.handle, &vk_state->swapchain.image_count, NULL);
	vkGetSwapchainImagesKHR(vk_state->device.logical, vk_state->swapchain.handle, &vk_state->swapchain.image_count, vk_state->swapchain.images);

	vk_state->swapchain.image_views_count = vk_state->swapchain.image_count;

	for (uint32_t i = 0; i < vk_state->swapchain.image_views_count; ++i) {
		if (vk_image_view_create(vk_state, vk_state->swapchain.images[i], vk_state->swapchain.format.format, VK_IMAGE_ASPECT_COLOR_BIT, &vk_state->swapchain.image_views[i]) == false) {
			LOG_ERROR("Failed to create Swapchain VkImageView");
			return false;
		}
	}

	LOG_INFO("Swapchain created");

	return true;
}

bool vk_recreate_swapchain(VulkanState *vk_state, struct platform *platform) {
	vkDeviceWaitIdle(vk_state->device.logical);

	for (uint32_t i = 0; i < vk_state->swapchain.framebuffer_count; ++i) {
		vkDestroyFramebuffer(vk_state->device.logical, vk_state->swapchain.framebuffers[i], NULL);
	}
	for (uint32_t i = 0; i < vk_state->swapchain.image_views_count; ++i) {
		vkDestroyImageView(vk_state->device.logical, vk_state->swapchain.image_views[i], NULL);
	}
	vkDestroySwapchainKHR(vk_state->device.logical, vk_state->swapchain.handle, NULL);
	vkDestroyImageView(vk_state->device.logical, vk_state->depth_image_view, NULL);
	vkDestroyImage(vk_state->device.logical, vk_state->depth_image, NULL);
	vkFreeMemory(vk_state->device.logical, vk_state->depth_image_memory, NULL);

	if (vk_create_swapchain(vk_state, platform) == false)
		return false;
	if (vk_create_depth_resources(vk_state) == false)
		return false;
	if (vk_create_framebuffers(vk_state) == false)
		return false;

	return true;
}

VkSurfaceFormatKHR swapchain_select_surface_format(VkSurfaceFormatKHR *formats, uint32_t count) {
	for (uint32_t i = 0; i < count; i++) {
		if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			return formats[i];
	}

	return formats[0];
}

VkPresentModeKHR swapchain_select_present_mode(VkPresentModeKHR *modes, uint32_t count) {
	for (uint32_t i = 0; i < count; i++) {
		if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			return modes[i];
		}
	}

	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D swapchain_select_extent(struct platform *platform, const VkSurfaceCapabilitiesKHR *capabilities) {
	if (capabilities->currentExtent.width != UINT32_MAX)
		return capabilities->currentExtent;
	else
		return (VkExtent2D){ platform->physical_width, platform->physical_height };
}
