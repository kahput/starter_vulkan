#include "core/logger.h"
#include "vk_renderer.h"

#include "core/arena.h"
#include <stdbool.h>
#include <vulkan/vulkan_core.h>

bool vk_create_image_views(struct arena *arena, VKRenderer *renderer) {
	renderer->image_views_count = renderer->swapchain_images_count;
	renderer->image_views = arena_push_array_zero(arena, VkImageView, renderer->image_views_count);

	for (uint32_t i = 0; i < renderer->image_views_count; ++i) {
		VkImageViewCreateInfo image_view_create_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = renderer->swapchain_images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = renderer->swapchain_format.format,
			.components = {
			  .r = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .g = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .b = VK_COMPONENT_SWIZZLE_IDENTITY,
			  .a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
			  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			  .baseMipLevel = 0,
			  .levelCount = 1,
			  .baseArrayLayer = 0,
			  .layerCount = 1,
			}
		};

		if (vkCreateImageView(renderer->logical_device, &image_view_create_info, NULL, &renderer->image_views[i]) != VK_SUCCESS) {
			LOG_ERROR("Failed to create swapchain image view");
			arena_clear(arena);
			return false;
		}
	}

	return true;
}
