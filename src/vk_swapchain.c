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

bool query_swapchain_support(struct arena *arena, VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &details.capabilities);

	uint32_t offset = arena_size(arena);

	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &details.formats_count, NULL);
	if (details.formats_count == 0) {
		LOG_ERROR("No surface formats available");
		arena_pop(arena, offset);
		return false;
	}

	details.formats = arena_push_array_zero(arena, VkSurfaceFormatKHR, details.formats_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &details.formats_count, details.formats);

	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &details.modes_count, NULL);
	if (details.modes_count == 0) {
		LOG_ERROR("No surface modes available");
		arena_pop(arena, offset);
		return false;
	}

	details.present_modes = arena_push_array_zero(arena, VkPresentModeKHR, details.modes_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &details.modes_count, details.present_modes);

	LOG_INFO("Swapchain available");

	return true;
}

bool vk_create_swapchain(Arena *arena, VKRenderer *renderer, struct platform *platform) {
	uint32_t offset = arena_size(arena);
	query_swapchain_support(arena, renderer->physical_device, renderer->surface);

	renderer->swapchain.format = swapchain_select_surface_format(details.formats, details.formats_count);
	renderer->swapchain.present_mode = swapchain_select_present_mode(details.present_modes, details.modes_count);
	renderer->swapchain.extent = swapchain_select_extent(platform, &details.capabilities);

	arena_pop(arena, offset);

	QueueFamilyIndices indices = find_queue_families(arena, renderer);
	uint32_t queue_family_indices[] = { indices.graphic_family, indices.present_family };

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
		arena_pop(arena, offset);
		return false;
	}

	vkGetSwapchainImagesKHR(renderer->logical_device, renderer->swapchain.handle, &renderer->swapchain.image_count, NULL);
	renderer->swapchain.images = arena_push_array_zero(arena, VkImage, renderer->swapchain.image_count);
	vkGetSwapchainImagesKHR(renderer->logical_device, renderer->swapchain.handle, &renderer->swapchain.image_count, renderer->swapchain.images);

	LOG_INFO("Swapchain created successfully");

	return true;
}

bool vk_recreate_swapchain(struct arena *arena, VKRenderer *renderer, struct platform *platform) {
	vkDeviceWaitIdle(renderer->logical_device);

	for (uint32_t i = 0; i < renderer->framebuffer_count; ++i) {
		vkDestroyFramebuffer(renderer->logical_device, renderer->framebuffers[i], NULL);
	}
	for (uint32_t i = 0; i < renderer->image_views_count; ++i) {
		vkDestroyImageView(renderer->logical_device, renderer->image_views[i], NULL);
	}
	vkDestroySwapchainKHR(renderer->logical_device, renderer->swapchain.handle, NULL);
	arena_clear(arena);

	if (vk_create_swapchain(arena, renderer, platform) == false)
		return false;
	if (vk_create_image_views(arena, renderer) == false)
		return false;
	if (vk_create_framebuffers(arena, renderer) == false)
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
