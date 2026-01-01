#include "platform.h"

#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "common.h"
#include "core/logger.h"
#include "allocators/arena.h"

VkSurfaceFormatKHR swapchain_select_surface_format(VkSurfaceFormatKHR *formats, uint32_t count);
VkPresentModeKHR swapchain_select_present_mode(VkPresentModeKHR *modes, uint32_t count);
VkExtent2D swapchain_select_extent(uint32_t window_width, uint32_t window_height, const VkSurfaceCapabilitiesKHR *capabilities);

// static bool create_renderpass(VkDevice logical_device, VkFormat swapchain_image_format, VkRenderPass *renderpass);
static bool create_swapchain_image_views(VulkanContext *context);
// static bool create_swapchain_framebuffers(VulkanContext *context);

bool vulkan_swapchain_create(VulkanContext *context, uint32_t width, uint32_t height) {
	context->swapchain.format = swapchain_select_surface_format(context->device.swapchain_details.formats, context->device.swapchain_details.format_count);
	context->swapchain.present_mode = swapchain_select_present_mode(context->device.swapchain_details.present_modes, context->device.swapchain_details.present_mode_count);
	context->swapchain.extent = swapchain_select_extent(width, height, &context->device.swapchain_details.capabilities);

	uint32_t queue_family_indices[] = { context->device.graphics_index, context->device.present_index };

	VkSurfaceCapabilitiesKHR capabilities = context->device.swapchain_details.capabilities;

	uint32_t image_count = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
		image_count = capabilities.maxImageCount;

	image_count = min(image_count, SWAPCHAIN_IMAGE_COUNT);

	VkSwapchainCreateInfoKHR swapchain_create_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = context->surface,
		.minImageCount = image_count,
		.imageFormat = context->swapchain.format.format,
		.imageColorSpace = context->swapchain.format.colorSpace,
		.imageExtent = context->swapchain.extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = context->swapchain.present_mode,
		.clipped = VK_TRUE, // If clipped is VK_TRUE, pixels in the presentable images that correspond to regions of the target surface obscured will have undefined content when read back.
	};

	if (queue_family_indices[0] != queue_family_indices[1]) {
		swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapchain_create_info.queueFamilyIndexCount = 2;
		swapchain_create_info.pQueueFamilyIndices = queue_family_indices;
	} else
		swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateSwapchainKHR(context->device.logical, &swapchain_create_info, NULL, &context->swapchain.handle) != VK_SUCCESS) {
		LOG_ERROR("Failed to create SwapchainKHR handle");
		return false;
	}

	vkGetSwapchainImagesKHR(context->device.logical, context->swapchain.handle, &context->swapchain.images.count, NULL);
	vkGetSwapchainImagesKHR(context->device.logical, context->swapchain.handle, &context->swapchain.images.count, context->swapchain.images.handles);

	create_swapchain_image_views(context);

	return true;
}

bool vulkan_swapchain_recreate(VulkanContext *context, uint32_t new_width, uint32_t new_height) {
	vkDeviceWaitIdle(context->device.logical);

	for (uint32_t i = 0; i < context->swapchain.images.count; ++i) {
		vkDestroyImageView(context->device.logical, context->swapchain.images.views[i], NULL);
	}
	vkDestroySwapchainKHR(context->device.logical, context->swapchain.handle, NULL);
	if (context->depth_attachment.handle) {
		vkDestroyImageView(context->device.logical, context->depth_attachment.view, NULL);
		vkDestroyImage(context->device.logical, context->depth_attachment.handle, NULL);
		vkFreeMemory(context->device.logical, context->depth_attachment.memory, NULL);

		context->depth_attachment.view = NULL;
		context->depth_attachment.handle = NULL;
		context->depth_attachment.memory = NULL;
	}

	if (context->color_attachment.handle) {
		vkDestroyImageView(context->device.logical, context->color_attachment.view, NULL);
		vkDestroyImage(context->device.logical, context->color_attachment.handle, NULL);
		vkFreeMemory(context->device.logical, context->color_attachment.memory, NULL);

		context->color_attachment.view = NULL;
		context->color_attachment.handle = NULL;
		context->color_attachment.memory = NULL;
	}

	if (vulkan_swapchain_create(context, new_width, new_height) == false)
		return false;
	if (vulkan_image_default_attachments_create(context) == false)
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
	return VK_PRESENT_MODE_FIFO_KHR;
	for (uint32_t i = 0; i < count; i++) {
		if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			return modes[i];
		}
	}

	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D swapchain_select_extent(uint32_t window_width, uint32_t window_height, const VkSurfaceCapabilitiesKHR *capabilities) {
	if (capabilities->currentExtent.width != UINT32_MAX)
		return capabilities->currentExtent;
	else
		return (VkExtent2D){ window_width, window_height };
}

bool create_swapchain_image_views(VulkanContext *context) {
	for (uint32_t i = 0; i < context->swapchain.images.count; ++i) {
		if (vulkan_image_view_create(context, context->swapchain.images.handles[i], context->swapchain.format.format, VK_IMAGE_ASPECT_COLOR_BIT, &context->swapchain.images.views[i]) == false) {
			LOG_ERROR("Failed to create Swapchain VkImageView");
			return false;
		}
	}

	return true;
}
