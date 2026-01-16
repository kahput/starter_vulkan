#include "vk_internal.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "core/debug.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vulkan_renderer_pass_create(VulkanContext *context, uint32_t store_index, RenderPassDesc desc) {
	VulkanPass *pass = NULL;
	VULKAN_GET_OR_RETURN(pass, context->pass_pool, store_index, MAX_RENDER_PASSES, VULKAN_RESOURCE_STATE_UNINITIALIZED);

	pass->color_attachment_count = desc.color_attachment_count;
	pass->enable_msaa = desc.enable_msaa;

	for (uint32_t color_index = 0; color_index < pass->color_attachment_count; ++color_index) {
		AttachmentDesc *src = &desc.color_attachments[color_index];
		VulkanAttachment *attachment = &pass->color_attachments[color_index];

		VulkanImage *image = &context->image_pool[src->texture];

		attachment->state = VULKAN_RESOURCE_STATE_INITIALIZED;
		attachment->image_index = src->texture;
		attachment->present = src->present;

		attachment->load = (VkAttachmentLoadOp)src->load,
		attachment->store = (VkAttachmentStoreOp)src->store,
		attachment->clear.color.float32[0] = src->clear.color[0];
		attachment->clear.color.float32[1] = src->clear.color[1];
		attachment->clear.color.float32[2] = src->clear.color[2];
		attachment->clear.color.float32[3] = src->clear.color[3];

		pass->color_formats[color_index] = image->info.format;
	}

	if (desc.use_depth) {
		VulkanImage *image = &context->image_pool[desc.depth_attachment.texture];
		AttachmentDesc *src = &desc.depth_attachment;
		VulkanAttachment *attachment = &pass->depth_attachment;

		attachment->state = VULKAN_RESOURCE_STATE_INITIALIZED;
		attachment->image_index = src->texture;
		attachment->load = (VkAttachmentLoadOp)src->load;
		attachment->store = (VkAttachmentStoreOp)src->store;
		attachment->clear.depthStencil.depth = src->clear.depth;

		pass->depth_format = image->info.format;
	}

	pass->state = VULKAN_RESOURCE_STATE_INITIALIZED;
	return true;
}

bool vulkan_renderer_pass_destroy(VulkanContext *context, uint32_t retrieve_index) {
	VulkanPass *pass = NULL;
	VULKAN_GET_OR_RETURN(pass, context->pass_pool, retrieve_index, MAX_RENDER_PASSES, true);

	*pass = (VulkanPass){ 0 };
	return true;
}

bool vulkan_renderer_pass_begin(VulkanContext *context, uint32_t retrieve_index) {
	VulkanPass *pass = NULL;
	VULKAN_GET_OR_RETURN(pass, context->pass_pool, retrieve_index, MAX_RENDER_PASSES, true);

	VkExtent2D extent = {
		context->swapchain.extent.width,
		context->swapchain.extent.height
	};

	bool use_msaa = pass->enable_msaa && context->device.sample_count > VK_SAMPLE_COUNT_1_BIT;

	VkRenderingAttachmentInfo color_attachments[4] = { 0 };
	ASSERT(pass->color_attachment_count <= 4);
	for (uint32_t color_index = 0; color_index < pass->color_attachment_count; ++color_index) {
		VulkanAttachment *cached = &pass->color_attachments[color_index];
		VkRenderingAttachmentInfo *info = &color_attachments[color_index];
		info->sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		info->imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		info->loadOp = cached->load;
		info->storeOp = cached->store;
		info->clearValue = cached->clear;

		if (use_msaa) {
			info->resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			info->resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			if (cached->present) {
				if (vulkan_image_msaa_scratch_ensure(
						context, &context->msaa_colors[color_index],
						context->swapchain.extent, context->swapchain.format.format, VK_IMAGE_ASPECT_COLOR_BIT) == false) {
					info->resolveMode = VK_RESOLVE_MODE_NONE;
					info->resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

					info->imageView = context->swapchain.images.views[context->image_index];
					continue;
				}

				info->imageView = context->msaa_colors[color_index].view;
				info->resolveImageView = context->swapchain.images.views[context->image_index];
			} else {
				VulkanImage *image = &context->image_pool[cached->image_index];
				vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

				extent.width = min(extent.width, image->width);
				extent.height = min(extent.height, image->height);

				if (vulkan_image_msaa_scratch_ensure(
						context, &context->msaa_colors[color_index],
						extent, image->info.format, VK_IMAGE_ASPECT_COLOR_BIT) == false) {
					info->resolveMode = VK_RESOLVE_MODE_NONE;
					info->resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					info->imageView = image->view;
					continue;
				}

				info->imageView = context->msaa_colors[color_index].view;
				info->resolveImageView = image->view;
			}
		} else {
			if (cached->present)
				info->imageView = context->swapchain.images.views[context->image_index];
			else {
				VulkanImage *image = &context->image_pool[cached->image_index];
				vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

				extent.width = min(extent.width, image->width);
				extent.height = min(extent.height, image->height);

				info->imageView = image->view;
			}
		}
	}

	VkRenderingAttachmentInfo depth_info = { 0 };
	if (pass->depth_attachment.state == VULKAN_RESOURCE_STATE_INITIALIZED) {
		VulkanAttachment *cached = &pass->depth_attachment;
		VulkanImage *image = &context->image_pool[cached->image_index];

		extent.width = min(extent.width, image->width);
		extent.height = min(extent.height, image->height);

		depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depth_info.loadOp = cached->load;
		depth_info.storeOp = cached->store;
		depth_info.clearValue = cached->clear;

		if (use_msaa) {
			depth_info.resolveMode = VK_RESOLVE_MODE_NONE;
			// depth_info.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			// depth_info.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			if (vulkan_image_msaa_scratch_ensure(
					context, &context->msaa_depth,
					extent, image->info.format, VK_IMAGE_ASPECT_DEPTH_BIT)) {
				// LOG_WARN("Vulkan: MSAA turned on for pass, passed in depth texture ignored");
				depth_info.imageView = context->msaa_depth.view;
			} else {
				depth_info.resolveMode = VK_RESOLVE_MODE_NONE;
				depth_info.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

				vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
				depth_info.imageView = image->view;
			}
		} else {
			vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			depth_info.imageView = image->view;
		}
	}

	VkRenderingInfo pass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { .extent = extent },
		.layerCount = 1,
		.colorAttachmentCount = pass->color_attachment_count,
		.pColorAttachments = color_attachments,
		.pDepthAttachment = pass->depth_attachment.state == VULKAN_RESOURCE_STATE_INITIALIZED ? &depth_info : NULL,
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
	return true;
}

bool vulkan_renderer_pass_end(VulkanContext *context) {
	vkCmdEndRendering(context->command_buffers[context->current_frame]);
	return true;
}
