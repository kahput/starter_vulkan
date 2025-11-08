#include "platform.h"
#include "vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"

#include <stdint.h>
#include <vulkan/vulkan_core.h>

typedef struct {
	VkSurfaceCapabilitiesKHR capabilities;

	VkSurfaceFormatKHR *formats;
	uint32_t formats_count;

	VkPresentModeKHR *present_modes;
	uint32_t modes_count;
} SwapChainSupportDetails;

static SwapChainSupportDetails details = { 0 };

VkSurfaceFormatKHR swapchain_select_surface_format(VkSurfaceFormatKHR *formats, uint32_t count);
VkPresentModeKHR swapchain_select_present_mode(VkPresentModeKHR *modes, uint32_t count);
VkExtent2D swapchain_select_extent(struct platform *platform, const VkSurfaceCapabilitiesKHR *capabilities);

bool query_swapchain_support(VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &details.capabilities);

	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &details.formats_count, NULL);
	if (details.formats_count == 0) {
		LOG_ERROR("No surface formats available");
		return false;
	}

	ArenaTemp temp = arena_get_scratch(NULL);
	details.formats = arena_push_array_zero(temp.arena, VkSurfaceFormatKHR, details.formats_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &details.formats_count, details.formats);

	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &details.modes_count, NULL);
	if (details.modes_count == 0) {
		LOG_ERROR("No surface modes available");
		arena_reset_scratch(temp);
		return false;
	}

	details.present_modes = arena_push_array_zero(temp.arena, VkPresentModeKHR, details.modes_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &details.modes_count, details.present_modes);

	LOG_INFO("Swapchain available");
	arena_reset_scratch(temp);

	return true;
}

bool vk_create_swapchain(VKRenderer *renderer, struct platform *platform) {
	query_swapchain_support(renderer->physical_device, renderer->surface);

	renderer->swapchain.format = swapchain_select_surface_format(details.formats, details.formats_count);
	renderer->swapchain.present_mode = swapchain_select_present_mode(details.present_modes, details.modes_count);
	renderer->swapchain.extent = swapchain_select_extent(platform, &details.capabilities);

	QueueFamilyIndices family_indices = find_queue_families(renderer);
	uint32_t queue_family_indices[] = { family_indices.graphics, family_indices.present };

	uint32_t image_count = details.capabilities.minImageCount + 1;
	if (details.capabilities.maxImageCount > 0 && image_count > details.capabilities.maxImageCount)
		image_count = details.capabilities.maxImageCount;

	VkSwapchainCreateInfoKHR swapchain_create_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = renderer->surface,
		.minImageCount = image_count,
		.imageFormat = renderer->swapchain.format.format,
		.imageColorSpace = renderer->swapchain.format.colorSpace,
		.imageExtent = renderer->swapchain.extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = details.capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = renderer->swapchain.present_mode,
		.clipped = VK_TRUE,
	};
	if (queue_family_indices[0] != queue_family_indices[1]) {
		swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapchain_create_info.queueFamilyIndexCount = 2;
		swapchain_create_info.pQueueFamilyIndices = queue_family_indices;
	} else
		swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateSwapchainKHR(renderer->logical_device, &swapchain_create_info, NULL, &renderer->swapchain.handle) != VK_SUCCESS) {
		LOG_ERROR("Failed to create swapchain");
		return false;
	}

	vkGetSwapchainImagesKHR(renderer->logical_device, renderer->swapchain.handle, &renderer->swapchain.image_count, NULL);
	vkGetSwapchainImagesKHR(renderer->logical_device, renderer->swapchain.handle, &renderer->swapchain.image_count, renderer->swapchain.images);

	LOG_INFO("Swapchain created");

	return true;
}

bool vk_recreate_swapchain(VKRenderer *renderer, struct platform *platform) {
	vkDeviceWaitIdle(renderer->logical_device);

	for (uint32_t i = 0; i < renderer->framebuffer_count; ++i) {
		vkDestroyFramebuffer(renderer->logical_device, renderer->framebuffers[i], NULL);
	}
	for (uint32_t i = 0; i < renderer->image_views_count; ++i) {
		vkDestroyImageView(renderer->logical_device, renderer->image_views[i], NULL);
	}
	vkDestroySwapchainKHR(renderer->logical_device, renderer->swapchain.handle, NULL);
	vkDestroyImageView(renderer->logical_device, renderer->depth_image_view, NULL);
	vkDestroyImage(renderer->logical_device, renderer->depth_image, NULL);
	vkFreeMemory(renderer->logical_device, renderer->depth_image_memory, NULL);

	if (vk_create_swapchain(renderer, platform) == false)
		return false;
	if (vk_create_swapchain_image_views(renderer) == false)
		return false;
	if (vk_create_depth_resources(renderer) == false)
		return false;
	if (vk_create_framebuffers(renderer) == false)
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
