#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vulkan_image_create(
	VulkanContext *context, VkSampleCountFlags sample_count,
	uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
	VkImageUsageFlags usage, TextureType type, VkMemoryPropertyFlags properties,
	VulkanImage *image) {
	image->type = type;
	image->width = width, image->height = height;
	image->layout = image->info.initialLayout;

	bool is_cubemap = type == TEXTURE_TYPE_CUBE ? true : false;
	image->info = (VkImageCreateInfo){
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.flags = is_cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
		.format = format,
		.extent = {
		  .width = width,
		  .height = height,
		  .depth = 1,
		},
		.mipLevels = 1,
		.arrayLayers = is_cubemap ? 6 : 1,
		.samples = sample_count,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

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

void vulkan_image_destroy(VulkanContext *context, VulkanImage *image) {
	if (!image || image->handle == VK_NULL_HANDLE)
		return;

	vkDestroyImageView(context->device.logical, image->view, NULL);
	vkDestroyImage(context->device.logical, image->handle, NULL);
	vkFreeMemory(context->device.logical, image->memory, NULL);
	*image = (VulkanImage){ 0 };
}

bool vulkan_image_view_create(VulkanContext *context, VkImageViewType type, VkImageAspectFlags aspect_flags, VulkanImage *image) {
	VkImageViewCreateInfo iv_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image->handle,
		.viewType = type,
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
		  .layerCount = type == VK_IMAGE_VIEW_TYPE_CUBE ? 6 : 1,
		}
	};
	image->aspect = aspect_flags;

	if (vkCreateImageView(context->device.logical, &iv_create_info, NULL, &image->view) != VK_SUCCESS) {
		return false;
	}

	return true;
}

void vulkan_image_transition_oneshot(VulkanContext *context, VkImage image, VkImageAspectFlags aspect, uint32_t layer_count, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkAccessFlags src_access, VkAccessFlags dst_access) {
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
		  .layerCount = layer_count,
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

void vulkan_image_transition(VulkanContext *context, VkCommandBuffer command_buffer, VkImage image, VkImageAspectFlags aspect, uint32_t layer_count, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, VkAccessFlags src_access, VkAccessFlags dst_access) {
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
		  .layerCount = layer_count,
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

void vulkan_image_transition_auto(VulkanImage *image, VkCommandBuffer command_buffer, VkImageLayout new_layout) {
	VkPipelineStageFlags src_stage = 0;
	VkPipelineStageFlags dst_stage = 0;
	VkAccessFlags src_access = 0;
	VkAccessFlags dst_access = 0;

	switch (image->layout) {
		case VK_IMAGE_LAYOUT_UNDEFINED:
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			src_access = 0;
			break;

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			src_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			src_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			src_access = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			src_stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			src_access = VK_ACCESS_SHADER_READ_BIT;
			break;

		default:
			LOG_WARN("Vuklan: unhandled old layout in %s, defaulting to ALL_COMMANDS", __func__);
			src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			src_access = VK_ACCESS_MEMORY_WRITE_BIT;
			break;
	}

	switch (new_layout) {
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dst_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			dst_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dst_access = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			dst_stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dst_access = 0;
			break;

		default:
			LOG_WARN("Vuklan: unhandled new layout in %s, defaulting to ALL_COMMANDS", __func__);
			dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			dst_access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			break;
	}

	VkImageMemoryBarrier image_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = src_access,
		.dstAccessMask = dst_access,
		.oldLayout = image->layout,
		.newLayout = new_layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image->handle,
		.subresourceRange = {
		  .aspectMask = image->aspect,
		  .baseMipLevel = 0,
		  .levelCount = 1,
		  .baseArrayLayer = 0,
		  .layerCount = image->type == TEXTURE_TYPE_CUBE ? 6 : 1,
		}
	};

	vkCmdPipelineBarrier(
		command_buffer,
		src_stage, dst_stage,
		0,
		0, NULL,
		0, NULL,
		1, &image_barrier);

	image->layout = new_layout;
}

bool vulkan_image_msaa_scratch_ensure(VulkanContext *context, VulkanImage *msaa, VkExtent2D extent, VkFormat format, VkSampleCountFlags sample_count, VkImageAspectFlags aspect) {
	bool recreate = false;

	if (msaa->info.extent.width != extent.width)
		recreate = true;
	if (msaa->info.extent.height != extent.height)
		recreate = true;
	if (msaa->info.samples != sample_count)
		recreate = true;
	if (msaa->info.format != format)
		recreate = true;

	if (recreate == false)
		return true;

	vulkan_image_destroy(context, msaa);

	VkImageUsageFlags usage =
		(aspect == VK_IMAGE_ASPECT_COLOR_BIT)
		? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
		: VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	VkImageLayout layout =
		(aspect == VK_IMAGE_ASPECT_COLOR_BIT)
		? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		: VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	if (vulkan_image_create(
			context, sample_count,
			extent.width, extent.height,
			format, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | usage, false,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			msaa) == false) {
		LOG_ERROR("Vulkan: failed to create MSAA color scratch image, aborting %s", __func__);
		return false;
	}

	if (!vulkan_image_view_create(context, VK_IMAGE_VIEW_TYPE_2D, aspect, msaa)) {
		LOG_ERROR("Vulkan: failed to create MSAA color scratch image view, aborting %s", __func__);
		return false;
	}
	vulkan_image_transition_auto(msaa, context->command_buffers[context->current_frame], layout);
	return true;
}
