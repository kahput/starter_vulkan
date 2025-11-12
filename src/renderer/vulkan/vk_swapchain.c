#include "platform.h"
#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"

#include "common.h"
#include <vulkan/vulkan_core.h>

VkSurfaceFormatKHR swapchain_select_surface_format(VkSurfaceFormatKHR *formats, uint32_t count);
VkPresentModeKHR swapchain_select_present_mode(VkPresentModeKHR *modes, uint32_t count);
VkExtent2D swapchain_select_extent(Platform *platform, const VkSurfaceCapabilitiesKHR *capabilities);

bool vk_create_swapchain(VulkanContext *context, Platform *platform) {
	context->swapchain.format = swapchain_select_surface_format(context->device.swapchain_details.formats, context->device.swapchain_details.format_count);
	context->swapchain.present_mode = swapchain_select_present_mode(context->device.swapchain_details.present_modes, context->device.swapchain_details.present_mode_count);
	context->swapchain.extent = swapchain_select_extent(platform, &context->device.swapchain_details.capabilities);

	uint32_t queue_family_indices[] = { context->device.graphics_index, context->device.present_index };

	uint32_t image_count = context->device.swapchain_details.capabilities.minImageCount + 1;
	if (context->device.swapchain_details.capabilities.maxImageCount > 0 && image_count > context->device.swapchain_details.capabilities.maxImageCount)
		image_count = context->device.swapchain_details.capabilities.maxImageCount;

	VkSwapchainCreateInfoKHR swapchain_create_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = context->surface,
		.minImageCount = image_count,
		.imageFormat = context->swapchain.format.format,
		.imageColorSpace = context->swapchain.format.colorSpace,
		.imageExtent = context->swapchain.extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = context->device.swapchain_details.capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = context->swapchain.present_mode,
		.clipped = VK_TRUE,
	};
	if (queue_family_indices[0] != queue_family_indices[1]) {
		swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapchain_create_info.queueFamilyIndexCount = 2;
		swapchain_create_info.pQueueFamilyIndices = queue_family_indices;
	} else
		swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateSwapchainKHR(context->device.logical, &swapchain_create_info, NULL, &context->swapchain.handle) != VK_SUCCESS) {
		LOG_ERROR("Failed to create swapchain");
		return false;
	}

	vkGetSwapchainImagesKHR(context->device.logical, context->swapchain.handle, &context->swapchain.image_count, NULL);
	vkGetSwapchainImagesKHR(context->device.logical, context->swapchain.handle, &context->swapchain.image_count, context->swapchain.images);

	context->swapchain.image_views_count = context->swapchain.image_count;

	for (uint32_t i = 0; i < context->swapchain.image_views_count; ++i) {
		if (vk_image_view_create(context, context->swapchain.images[i], context->swapchain.format.format, VK_IMAGE_ASPECT_COLOR_BIT, &context->swapchain.image_views[i]) == false) {
			LOG_ERROR("Failed to create Swapchain VkImageView");
			return false;
		}
	}

	LOG_INFO("Swapchain created");

	return true;
}

bool vk_recreate_swapchain(VulkanContext *context, Platform *platform) {
	vkDeviceWaitIdle(context->device.logical);

	for (uint32_t i = 0; i < context->swapchain.framebuffer_count; ++i) {
		vkDestroyFramebuffer(context->device.logical, context->swapchain.framebuffers[i], NULL);
	}
	for (uint32_t i = 0; i < context->swapchain.image_views_count; ++i) {
		vkDestroyImageView(context->device.logical, context->swapchain.image_views[i], NULL);
	}
	vkDestroySwapchainKHR(context->device.logical, context->swapchain.handle, NULL);
	vkDestroyImageView(context->device.logical, context->depth_image_view, NULL);
	vkDestroyImage(context->device.logical, context->depth_image, NULL);
	vkFreeMemory(context->device.logical, context->depth_image_memory, NULL);

	if (vk_create_swapchain(context, platform) == false)
		return false;
	if (vk_create_depth_resources(context) == false)
		return false;
	if (vk_create_framebuffers(context) == false)
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

VkExtent2D swapchain_select_extent(Platform *platform, const VkSurfaceCapabilitiesKHR *capabilities) {
	if (capabilities->currentExtent.width != UINT32_MAX)
		return capabilities->currentExtent;
	else
		return (VkExtent2D){ platform->physical_width, platform->physical_height };
}
