#include "platform.h"
#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"

#include "common.h"
#include <vulkan/vulkan_core.h>

VkSurfaceFormatKHR swapchain_select_surface_format(VkSurfaceFormatKHR *formats, uint32_t count);
VkPresentModeKHR swapchain_select_present_mode(VkPresentModeKHR *modes, uint32_t count);
VkExtent2D swapchain_select_extent(Platform *platform, const VkSurfaceCapabilitiesKHR *capabilities);

bool vk_create_swapchain(VulkanContext *ctx, Platform *platform) {
	ctx->swapchain.format = swapchain_select_surface_format(ctx->device.details.formats, ctx->device.details.format_count);
	ctx->swapchain.present_mode = swapchain_select_present_mode(ctx->device.details.present_modes, ctx->device.details.present_mode_count);
	ctx->swapchain.extent = swapchain_select_extent(platform, &ctx->device.details.capabilities);

	uint32_t queue_family_indices[] = { ctx->device.graphics_index, ctx->device.present_index };

	uint32_t image_count = ctx->device.details.capabilities.minImageCount + 1;
	if (ctx->device.details.capabilities.maxImageCount > 0 && image_count > ctx->device.details.capabilities.maxImageCount)
		image_count = ctx->device.details.capabilities.maxImageCount;

	VkSwapchainCreateInfoKHR swapchain_create_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = ctx->surface,
		.minImageCount = image_count,
		.imageFormat = ctx->swapchain.format.format,
		.imageColorSpace = ctx->swapchain.format.colorSpace,
		.imageExtent = ctx->swapchain.extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = ctx->device.details.capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = ctx->swapchain.present_mode,
		.clipped = VK_TRUE,
	};
	if (queue_family_indices[0] != queue_family_indices[1]) {
		swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapchain_create_info.queueFamilyIndexCount = 2;
		swapchain_create_info.pQueueFamilyIndices = queue_family_indices;
	} else
		swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateSwapchainKHR(ctx->device.logical, &swapchain_create_info, NULL, &ctx->swapchain.handle) != VK_SUCCESS) {
		LOG_ERROR("Failed to create swapchain");
		return false;
	}

	vkGetSwapchainImagesKHR(ctx->device.logical, ctx->swapchain.handle, &ctx->swapchain.image_count, NULL);
	vkGetSwapchainImagesKHR(ctx->device.logical, ctx->swapchain.handle, &ctx->swapchain.image_count, ctx->swapchain.images);

	ctx->swapchain.image_views_count = ctx->swapchain.image_count;

	for (uint32_t i = 0; i < ctx->swapchain.image_views_count; ++i) {
		if (vk_image_view_create(ctx, ctx->swapchain.images[i], ctx->swapchain.format.format, VK_IMAGE_ASPECT_COLOR_BIT, &ctx->swapchain.image_views[i]) == false) {
			LOG_ERROR("Failed to create Swapchain VkImageView");
			return false;
		}
	}

	LOG_INFO("Swapchain created");

	return true;
}

bool vk_recreate_swapchain(VulkanContext *ctx, Platform *platform) {
	vkDeviceWaitIdle(ctx->device.logical);

	for (uint32_t i = 0; i < ctx->swapchain.framebuffer_count; ++i) {
		vkDestroyFramebuffer(ctx->device.logical, ctx->swapchain.framebuffers[i], NULL);
	}
	for (uint32_t i = 0; i < ctx->swapchain.image_views_count; ++i) {
		vkDestroyImageView(ctx->device.logical, ctx->swapchain.image_views[i], NULL);
	}
	vkDestroySwapchainKHR(ctx->device.logical, ctx->swapchain.handle, NULL);
	vkDestroyImageView(ctx->device.logical, ctx->depth_image_view, NULL);
	vkDestroyImage(ctx->device.logical, ctx->depth_image, NULL);
	vkFreeMemory(ctx->device.logical, ctx->depth_image_memory, NULL);

	if (vk_create_swapchain(ctx, platform) == false)
		return false;
	if (vk_create_depth_resources(ctx) == false)
		return false;
	if (vk_create_framebuffers(ctx) == false)
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
