#include "vk_internal.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "core/debug.h"

VkSampleCountFlags to_sample_count(uint32_t sample_count);

bool vulkan_renderer_draw_list_begin(VulkanContext *context, DrawListDesc desc) {
	VkExtent2D extent = {
		context->swapchain.extent.width,
		context->swapchain.extent.height
	};

	bool use_msaa = desc.msaa_level > 1 && context->device.sample_count > VK_SAMPLE_COUNT_1_BIT;
	VkSampleCountFlags sample_count = min(to_sample_count(desc.msaa_level), context->device.sample_count);

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
		dst->loadOp = (VkAttachmentLoadOp)src->load;
		dst->storeOp = (VkAttachmentStoreOp)src->store;

		dst->clearValue.color.float32[0] = src->clear.color.x;
		dst->clearValue.color.float32[1] = src->clear.color.y;
		dst->clearValue.color.float32[2] = src->clear.color.z;
		dst->clearValue.color.float32[3] = src->clear.color.w;

		// Present
		if (src->texture.id == 0) {
			context->bound_pass.color_formats[color_index] = context->swapchain.format.format;

			if (use_msaa) {
				dst->resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
				dst->resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				ASSERT(
					vulkan_image_msaa_scratch_ensure(context, &context->msaa_colors[color_index],
						context->swapchain.extent, context->swapchain.format.format, sample_count, VK_IMAGE_ASPECT_COLOR_BIT));

				dst->imageView = context->msaa_colors[color_index].view;
				dst->resolveImageView = context->swapchain.images.views[context->image_index];
			} else
				dst->imageView = context->swapchain.images.views[context->image_index];

		} else {
			VulkanImage *image = NULL;
			VULKAN_GET_OR_RETURN(image, context->image_pool, src->texture, MAX_TEXTURES, true, false);
			vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

			context->bound_pass.color_formats[color_index] = image->info.format;

			extent.width = min(extent.width, image->width);
			extent.height = min(extent.height, image->height);

			if (use_msaa) {
				dst->resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
				dst->resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

				ASSERT(vulkan_image_msaa_scratch_ensure(
					context, &context->msaa_colors[color_index],
					extent, image->info.format, sample_count, VK_IMAGE_ASPECT_COLOR_BIT));

				dst->imageView = context->msaa_colors[color_index].view;
				dst->resolveImageView = image->view;
			} else
				dst->imageView = image->view;
		}
	}

	VkRenderingAttachmentInfo depth_info = { 0 };
	if (desc.depth_attachment.texture.id) {
		AttachmentDesc *src = &desc.depth_attachment;

		VulkanImage *image = NULL;
		VULKAN_GET_OR_RETURN(image, context->image_pool, src->texture, MAX_TEXTURES, true, false);

		context->bound_pass.depth_format = image->info.format;

		extent.width = min(extent.width, image->width);
		extent.height = min(extent.height, image->height);

		depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depth_info.loadOp = (VkAttachmentLoadOp)src->load;
		depth_info.storeOp = (VkAttachmentStoreOp)src->store;
		depth_info.clearValue.depthStencil.depth = src->clear.depth;

		if (use_msaa) {
			depth_info.resolveMode = VK_RESOLVE_MODE_NONE;
			// depth_info.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			// depth_info.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			ASSERT(vulkan_image_msaa_scratch_ensure(
				context, &context->msaa_depth,
				extent, image->info.format, sample_count, VK_IMAGE_ASPECT_DEPTH_BIT));

			depth_info.imageView = context->msaa_depth.view;
		} else {
			vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			depth_info.imageView = image->view;
		}
	}

	VkRenderingInfo pass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { .extent = extent },
		.layerCount = 1,
		.colorAttachmentCount = desc.color_attachment_count,
		.pColorAttachments = color_attachments,
		.pDepthAttachment = desc.depth_attachment.texture.id ? &depth_info : NULL,
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

bool vulkan_renderer_draw_list_end(VulkanContext *context) {
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
