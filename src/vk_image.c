#include "core/logger.h"
#include "vk_renderer.h"

#include "core/arena.h"
#include <stdbool.h>
#include <vulkan/vulkan_core.h>

bool vk_create_image(
	VKRenderer *renderer, uint32_t *family_indices, uint32_t family_count,
	uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
	VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
	VkImage *image, VkDeviceMemory *memory) {
	VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = {
		  .width = width,
		  .height = height,
		  .depth = 1,
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = family_count > 1 ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = family_count,
		.pQueueFamilyIndices = family_indices,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	if (vkCreateImage(renderer->logical_device, &image_info, NULL, image) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan Texture");
		return false;
	}

	LOG_INFO("VkImage created");

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements(renderer->logical_device, *image, &memory_requirements);

	VkMemoryAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = find_memory_type(renderer->physical_device, memory_requirements.memoryTypeBits, properties),
	};

	if (vkAllocateMemory(renderer->logical_device, &allocate_info, NULL, memory) != VK_SUCCESS) {
		LOG_ERROR("Failed to allocate VkDeviceMemory for VkImage");
		return false;
	}

	vkBindImageMemory(renderer->logical_device, *image, *memory, 0);

	LOG_INFO("VkDeviceMemory[%d] allocated for VkImage", memory_requirements.size);

	return true;
}

bool vk_create_image_view(VKRenderer *renderer, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, VkImageView *view) {
	VkImageViewCreateInfo iv_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.components = {
		  .r = VK_COMPONENT_SWIZZLE_IDENTITY,
		  .g = VK_COMPONENT_SWIZZLE_IDENTITY,
		  .b = VK_COMPONENT_SWIZZLE_IDENTITY,
		  .a = VK_COMPONENT_SWIZZLE_IDENTITY,
		},
		.subresourceRange = {
		  .aspectMask = aspect_flags,
		  .baseMipLevel = 0,
		  .levelCount = 1,
		  .baseArrayLayer = 0,
		  .layerCount = 1,
		}
	};

	if (vkCreateImageView(renderer->logical_device, &iv_create_info, NULL, view) != VK_SUCCESS) {
		return false;
	}

	return true;
}

bool vk_create_swapchain_image_views(struct arena *arena, VKRenderer *renderer) {
	uint32_t offset = arena_size(arena);

	renderer->image_views_count = renderer->swapchain.image_count;
	renderer->image_views = arena_push_array_zero(arena, VkImageView, renderer->image_views_count);

	for (uint32_t i = 0; i < renderer->image_views_count; ++i) {
		if (vk_create_image_view(renderer, renderer->swapchain.images[i], renderer->swapchain.format.format, VK_IMAGE_ASPECT_COLOR_BIT, &renderer->image_views[i]) == false) {
			LOG_ERROR("Failed to create Swapchain VkImageView");
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

bool vk_create_depth_resources(struct arena *arena, VKRenderer *renderer) {
	uint32_t index = find_queue_families(arena, renderer).graphics;

	vk_create_image(
		renderer, &index, 1,
		renderer->swapchain.extent.width, renderer->swapchain.extent.height,
		VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		&renderer->depth_image, &renderer->depth_image_memory);

	vk_create_image_view(renderer, renderer->depth_image, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, &renderer->depth_image_view);

	return true;
}
