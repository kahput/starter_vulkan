#include "core/logger.h"
#include "renderer/vk_renderer.h"

#include "common.h"
#include "allocators/arena.h"
#include <vulkan/vulkan_core.h>

bool vulkan_image_create(
	VulkanContext *context, uint32_t *family_indices, uint32_t family_count,
	uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
	VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
	VulkanImage *image) {
	image->format = format;

	VkImageCreateInfo image_info = (VkImageCreateInfo){
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = image->format,
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

	if (vkCreateImage(context->device.logical, &image_info, NULL, &image->handle) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan Texture");
		return false;
	}

	LOG_INFO("VkImage created");

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements(context->device.logical, image->handle, &memory_requirements);

	VkMemoryAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = find_memory_type(context->device.physical, memory_requirements.memoryTypeBits, properties),
	};

	if (vkAllocateMemory(context->device.logical, &allocate_info, NULL, &image->memory) != VK_SUCCESS) {
		LOG_ERROR("Failed to allocate VkDeviceMemory for VkImage");
		return false;
	}

	vkBindImageMemory(context->device.logical, image->handle, image->memory, 0);

	LOG_INFO("VkDeviceMemory[%d] allocated for VkImage", memory_requirements.size);

	return true;
}

bool vulkan_image_view_create(VulkanContext *context, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, VkImageView *view) {
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

	if (vkCreateImageView(context->device.logical, &iv_create_info, NULL, view) != VK_SUCCESS) {
		return false;
	}

	return true;
}

bool vulkan_image_transition(VulkanContext *context, VkImage image, VkImageAspectFlags aspect, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkAccessFlags src_access, VkAccessFlags dst_access) {
	VkCommandBuffer command_buffer;
	vulkan_begin_single_time_commands(context, context->graphics_command_pool, &command_buffer);

	VkImageMemoryBarrier image_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = src_access,
		.dstAccessMask = dst_access,
		.oldLayout = old_layout,
		.newLayout = new_layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {
		  .aspectMask = aspect,
		  .baseMipLevel = 0,
		  .levelCount = 1,
		  .baseArrayLayer = 0,
		  .layerCount = 1,
		}
	};

	vkCmdPipelineBarrier(
		command_buffer,
		src_stage, dst_stage,
		0,
		0, NULL,
		0, NULL,
		1, &image_barrier);

	vulkan_end_single_time_commands(context, context->device.graphics_queue, context->graphics_command_pool, &command_buffer);

	return true;
}

bool vulkan_buffer_to_image(VulkanContext *context, VkBuffer src, VkImage dst, uint32_t width, uint32_t height) {
	VkCommandBuffer command_buffer;
	vulkan_begin_single_time_commands(context, context->transfer_command_pool, &command_buffer);

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

	vulkan_end_single_time_commands(context, context->device.transfer_queue, context->transfer_command_pool, &command_buffer);
	return true;
}
