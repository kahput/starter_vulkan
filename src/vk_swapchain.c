#include "core/arena.h"
#include "core/logger.h"
#include "vk_renderer.h"

typedef struct {
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR *formats;
	VkPresentModeKHR *present_modes;
} SwapChainSupportDetails;

VkSurfaceFormatKHR select_swap_surface_format(VkSurfaceFormatKHR *formats, uint32_t count);
VkPresentModeKHR select_swap_present_mode(VkPresentModeKHR *modes, uint32_t count);
VkExtent2D select_swap_extent(const VkSurfaceCapabilitiesKHR *capabilities);

bool vk_query_swapchain_support(Arena *arena, VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
	SwapChainSupportDetails details = { 0 };

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &details.capabilities);

	uint32_t format_count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, NULL);

	details.formats = arena_push_array_zero(arena, VkSurfaceFormatKHR, format_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, details.formats);

	if (format_count == 0) {
		LOG_ERROR("No surface formats available");
		arena_clear(arena);
		return false;
	}

	uint32_t mode_count = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, NULL);

	details.present_modes = arena_push_array_zero(arena, VkPresentModeKHR, mode_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, details.present_modes);

	if (mode_count == 0) {
		LOG_ERROR("No surface modes available");
		arena_clear(arena);
		return false;
	}

	LOG_INFO("Swapchain available");

	return true;
}

VkSurfaceFormatKHR select_swap_surface_format(VkSurfaceFormatKHR *formats, uint32_t count) {
	for (uint32_t i = 0; i < count; i++) {
		if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			return formats[i];
	}

	return formats[0];
}

VkPresentModeKHR select_swap_present_mode(VkPresentModeKHR *modes, uint32_t count) {
	for (uint32_t i = 0; i < count; i++) {
		if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			return modes[i];
		}
	}

	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D select_swap_extent(const VkSurfaceCapabilitiesKHR *capabilities) {
	return (VkExtent2D){0};
}
