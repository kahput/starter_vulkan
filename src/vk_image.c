#include "core/logger.h"
#include "vk_renderer.h"

#include "core/arena.h"
#include <stdbool.h>
#include <vulkan/vulkan_core.h>

bool vk_create_image_views(struct arena *arena, VKRenderer *renderer) {
	uint32_t offset = arena_size(arena);

	renderer->image_views_count = renderer->swapchain.image_count;
	renderer->image_views = arena_push_array_zero(arena, VkImageView, renderer->image_views_count);

	for (uint32_t i = 0; i < renderer->image_views_count; ++i) {
		VkImageViewCreateInfo image_view_create_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = renderer->swapchain.images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = renderer->swapchain.format.format,
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
			arena_pop(arena, offset);
			return false;
		}
	}

	return true;
}

bool vk_transition_image_layout(VKRenderer *renderer, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout) {
	VkCommandBuffer command_buffer;
	vk_begin_single_time_commands(renderer, renderer->graphics_command_pool, &command_buffer);

	VkImageMemoryBarrier image_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = old_layout,
		.newLayout = new_layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {
		  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		  .baseMipLevel = 0,
		  .levelCount = 1,
		  .baseArrayLayer = 0,
		  .layerCount = 1,
		}
	};

	VkPipelineStageFlags src_stage = 0, dst_stage = 0;
	if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		image_barrier.srcAccessMask = 0;
		image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		image_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}

	vkCmdPipelineBarrier(
		command_buffer,
		src_stage, dst_stage, // TODO:
		0,
		0, NULL,
		0, NULL,
		1, &image_barrier);

	vk_end_single_time_commands(renderer, renderer->graphics_queue, renderer->graphics_command_pool, &command_buffer);

	return true;
}

bool vk_copy_buffer_to_image(VKRenderer *renderer, VkBuffer src, VkImage dst, uint32_t width, uint32_t height) {
	VkCommandBuffer command_buffer;
	vk_begin_single_time_commands(renderer, renderer->transfer_command_pool, &command_buffer);

	VkBufferImageCopy region = {
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource = {
		  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		  .mipLevel = 0,
		  .baseArrayLayer = 0,
		  .layerCount = 1,
		},
		.imageOffset = { 0 },
		.imageExtent = { .width = width, .height = height, .depth = 1 },
	};

	vkCmdCopyBufferToImage(command_buffer, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	vk_end_single_time_commands(renderer, renderer->transfer_queue, renderer->transfer_command_pool, &command_buffer);
	return true;
}
