#include "vk_internal.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "core/debug.h"
#include <vulkan/vulkan_core.h>

VkSampleCountFlags to_sample_count(uint32_t sample_count);

bool vulkan_drawlist_begin(VulkanContext *context, DrawListDesc desc) {
	VkExtent2D extent = {
		context->swapchain.extent.width,
		context->swapchain.extent.height
	};

	bool use_msaa = desc.msaa_level > 1 && context->device.sample_count > VK_SAMPLE_COUNT_1_BIT;
	VkSampleCountFlags sample_count = MIN(to_sample_count(desc.msaa_level), context->device.sample_count);

	context->bound_pass = (VulkanPass){
		.sample_count = sample_count,
	};

	VkRenderingAttachmentInfo color_attachments[4] = { 0 };
	ASSERT(desc.color_attachment_count <= 4);
	for (uint32_t color_index = 0; color_index < desc.color_attachment_count; ++color_index) {
		AttachmentDesc *src = &desc.color_attachments[color_index];
		VkRenderingAttachmentInfo *dst = &color_attachments[color_index];
		dst->sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		dst->imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		dst->clearValue.color.float32[0] = src->clear.color.x;
		dst->clearValue.color.float32[1] = src->clear.color.y;
		dst->clearValue.color.float32[2] = src->clear.color.z;
		dst->clearValue.color.float32[3] = src->clear.color.w;

		// Present
		if (src->target.id == 0) {
			context->bound_pass.color_formats[color_index] = context->swapchain.format.format;
			dst->storeOp = (VkAttachmentStoreOp)src->store;
			dst->loadOp = (VkAttachmentLoadOp)src->load;

			if (use_msaa) {
				ASSERT(context->frame_target_count < countof(context->frame_targets));
				VulkanImage *target = &context->frame_targets[context->frame_target_count++];

				dst->resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
				dst->resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

				bool result = vulkan_image_scratch_ensure(context, target,
					context->swapchain.extent, context->swapchain.format.format, sample_count, VK_IMAGE_ASPECT_COLOR_BIT);
				ASSERT(result);

				dst->imageView = target->view;
				dst->resolveImageView = context->swapchain.images.views[context->image_index];
			} else
				dst->imageView = context->swapchain.images.views[context->image_index];

		} else {
			VulkanImage *image = NULL;
			VULKAN_GET_OR_RETURN(image, context->image_pool, src->target, MAX_TEXTURES, true, false);
			vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

			context->bound_pass.color_formats[color_index] = image->info.format;
			dst->loadOp = (VkAttachmentLoadOp)src->load;
			dst->storeOp = (VkAttachmentStoreOp)src->store;

			extent.width = MIN(extent.width, image->width);
			extent.height = MIN(extent.height, image->height);

			if (use_msaa) {
				ASSERT(context->frame_target_count < countof(context->frame_targets));
				VulkanImage *target = &context->frame_targets[context->frame_target_count++];

				dst->resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
				dst->resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

				ASSERT(vulkan_image_scratch_ensure(
					context, target,
					extent, image->info.format, sample_count, VK_IMAGE_ASPECT_COLOR_BIT));

				dst->imageView = target->view;
				dst->resolveImageView = image->view;
			} else {
				dst->imageView = image->view;
			}
		}
	}

	VkRenderingAttachmentInfo depth_info = { 0 };
	if (desc.use_depth) {
		VulkanImage *depth_target = NULL;
		if (desc.depth_attachment.target.id == 0) {
			depth_target = &context->frame_targets[context->frame_target_count++];
			ASSERT(context->frame_target_count < countof(context->frame_targets));
			vulkan_image_scratch_ensure(
				context, depth_target,
				extent, context->device.depth_format, sample_count, VK_IMAGE_ASPECT_DEPTH_BIT);
		} else {
			VULKAN_GET_OR_RETURN(depth_target, context->image_pool, desc.depth_attachment.target, MAX_TEXTURES, true, false);
			vulkan_image_transition_auto(depth_target, context->command_buffers[context->current_frame], VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		}

		context->bound_pass.depth_format = context->device.depth_format;

		depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depth_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_info.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_info.clearValue.depthStencil.depth = 1.0f;

		depth_info.imageView = depth_target->view;
	}

	VkRenderingInfo pass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { .extent = extent },
		.layerCount = 1,
		.colorAttachmentCount = desc.color_attachment_count,
		.pColorAttachments = color_attachments,
		.pDepthAttachment = desc.use_depth ? &depth_info : NULL,
	};

	vkCmdBeginRendering(context->command_buffers[context->current_frame], &pass_info);

	VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = extent.width,
		.height = extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(context->command_buffers[context->current_frame], 0, 1, &viewport);

	VkRect2D scissor = {
		.offset = { 0.0f, 0.0f },
		.extent = extent
	};
	vkCmdSetScissor(context->command_buffers[context->current_frame], 0, 1, &scissor);

	context->bound_pass.state = VULKAN_RESOURCE_STATE_INITIALIZED;
	return true;
}

bool vulkan_drawlist_end(VulkanContext *context) {
	vkCmdEndRendering(context->command_buffers[context->current_frame]);

	context->bound_pass = (VulkanPass){ 0 };
	return true;
}

VkSampleCountFlags to_sample_count(uint32_t sample_count) {
	uint result = 1;
	while (result < sample_count)
		result *= 2;

	return result;
}
