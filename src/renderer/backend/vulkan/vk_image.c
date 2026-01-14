#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "common.h"
#include "core/logger.h"
#include "core/arena.h"
#include <vulkan/vulkan_core.h>

bool vulkan_image_create(
	VulkanContext *context, VkSampleCountFlags sample_count,
	uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
	VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
	VulkanImage *image) {
	image->info = (VkImageCreateInfo){
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
		.samples = sample_count,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};
	image->layout = image->info.initialLayout;

	if (vkCreateImage(context->device.logical, &image->info, NULL, &image->handle) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan Texture");
		return false;
	}

	LOG_INFO("VkImage created");

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements(context->device.logical, image->handle, &memory_requirements);

	VkMemoryAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = vulkan_memory_type_find(context->device.physical, memory_requirements.memoryTypeBits, properties),
	};

	if (vkAllocateMemory(context->device.logical, &allocate_info, NULL, &image->memory) != VK_SUCCESS) {
		LOG_ERROR("Failed to allocate VkDeviceMemory for VkImage");
		return false;
	}

	vkBindImageMemory(context->device.logical, image->handle, image->memory, 0);

	LOG_INFO("VkDeviceMemory[%d] allocated for VkImage", memory_requirements.size);

	return true;
}

bool vulkan_image_view_create(VulkanContext *context, VkImageAspectFlags aspect_flags, VulkanImage *image) {
	VkImageViewCreateInfo iv_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image->handle,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = image->info.format,
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
    image->aspect = aspect_flags;

	if (vkCreateImageView(context->device.logical, &iv_create_info, NULL, &image->view) != VK_SUCCESS) {
		return false;
	}

	return true;
}

void vulkan_image_transition_oneshot(VulkanContext *context, VkImage image, VkImageAspectFlags aspect, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkAccessFlags src_access, VkAccessFlags dst_access) {
	VkCommandBuffer command_buffer;
	vulkan_command_oneshot_begin(context, context->graphics_command_pool, &command_buffer);

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

	vulkan_command_oneshot_end(context, context->device.graphics_queue, context->graphics_command_pool, &command_buffer);
}

void vulkan_image_transition(VulkanContext *context, VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags aspect, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkAccessFlags src_access, VkAccessFlags dst_access) {
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
}

bool vulkan_image_default_attachments_create(VulkanContext *context) {
	// vulkan_image_create(
	// 	context, context->sample_count,
	// 	context->swapchain.extent.width, context->swapchain.extent.height,
	// 	context->swapchain.format.format, VK_IMAGE_TILING_OPTIMAL,
	// 	VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	// 	&context->color_attachment);
	// vulkan_image_view_create(context, context->color_attachment.handle, context->swapchain.format.format, VK_IMAGE_ASPECT_COLOR_BIT, &context->color_attachment.view);
	// vulkan_image_transition_oneshot(
	// 	context, context->color_attachment.handle, VK_IMAGE_ASPECT_COLOR_BIT,
	// 	VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	// 	VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
	// 	0, 0);
	//
	// vulkan_image_create(
	// 	context, context->sample_count,
	// 	context->swapchain.extent.width, context->swapchain.extent.height,
	// 	VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
	// 	VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	// 	&context->depth_attachment);
	// vulkan_image_view_create(context, context->depth_attachment.handle, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, &context->depth_attachment.view);
	//
	// vulkan_image_transition_oneshot(
	// 	context, context->depth_attachment.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
	// 	VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	// 	VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
	// 	0, 0);

	return true;
}
