#include "vk_internal.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "core/debug.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vulkan_renderer_pass_create(VulkanContext *context, uint32_t store_index, uint32_t global_resource, RenderPassDesc *desc) {
	VulkanPass *pass = NULL;
	VK_GET_OR_RETURN(pass, context->pass_pool, store_index, MAX_RENDER_PASSES, VULKAN_RESOURCE_STATE_UNINITIALIZED);

	pass->color_attachment_count = desc->color_attachment_count;
	for (uint32_t color_index = 0; color_index < pass->color_attachment_count; ++color_index) {
		AttachmentDesc *src = &desc->color_attachments[color_index];
		VulkanAttachment *attachment = &pass->color_attachments[color_index];

		VulkanImage *color_image = &context->image_pool[src->texture];

		attachment->state = VULKAN_RESOURCE_STATE_INITIALIZED;
		attachment->image_index = src->texture;
		attachment->present = src->present;
		attachment->info = (VkRenderingAttachmentInfo){
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = color_image->view,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.loadOp = (VkAttachmentLoadOp)src->load,
			.storeOp = (VkAttachmentStoreOp)src->store,
			.clearValue.color.float32 = {
			  src->clear.color[0],
			  src->clear.color[1],
			  src->clear.color[2],
			  src->clear.color[3],
			},
		};
	}

	VulkanImage *depth_image = &context->image_pool[desc->depth_attachment.texture];
	if (desc->use_depth) {
		AttachmentDesc *src = &desc->depth_attachment;
		VulkanAttachment *attachment = &pass->depth_attachment;

		attachment->state = VULKAN_RESOURCE_STATE_INITIALIZED;
		attachment->image_index = src->texture;
		attachment->info = (VkRenderingAttachmentInfo){
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = depth_image->view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.loadOp = (VkAttachmentLoadOp)src->load,
			.storeOp = (VkAttachmentStoreOp)src->store,
			.clearValue.depthStencil.depth = src->clear.depth,
		};
	}

	pass->state = VULKAN_RESOURCE_STATE_INITIALIZED;
	return true;
}

bool vulkan_renderer_pass_destroy(VulkanContext *context, uint32_t retrieve_index) {
	VulkanPass *pass = NULL;
	VK_GET_OR_RETURN(pass, context->pass_pool, retrieve_index, MAX_RENDER_PASSES, true);

	*pass = (VulkanPass){ 0 };
	return true;
}

bool vulkan_renderer_pass_begin(VulkanContext *context, uint32_t retrieve_index) {
	VulkanPass *pass = NULL;
	VK_GET_OR_RETURN(pass, context->pass_pool, retrieve_index, MAX_RENDER_PASSES, true);

	VkExtent2D extent = {
		.width = pass->width == 0 ? context->swapchain.extent.width : pass->width,
		.height = pass->height == 0 ? context->swapchain.extent.height : pass->height
	};

	VkRenderingAttachmentInfo color_attachments[4] = { 0 };
	for (uint32_t color_index = 0; color_index < pass->color_attachment_count; ++color_index) {
		VulkanAttachment *cached = &pass->color_attachments[color_index];
		VulkanImage *image = &context->image_pool[cached->image_index];

		if (cached->present) {
			cached->info.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			cached->info.resolveImageView = context->swapchain.images.views[context->image_index];
			cached->info.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		if ((image->width != MATCH_SWAPCHAIN && image->width < extent.width) ||
			(image->height != MATCH_SWAPCHAIN && image->height < extent.height)) {
			LOG_WARN("Vulkan: pass attachment is smaller than draw area, resizing down");
			extent.width = image->width;
			extent.height = image->height;
		}

		VkRenderingAttachmentInfo *info = &color_attachments[color_index];
		info->sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		info->imageView = image->view;
		info->imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		info->loadOp = cached->info.loadOp;
		info->storeOp = cached->info.storeOp;
		info->clearValue = cached->info.clearValue;

		if (cached->present) {
			info->resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			info->resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			info->resolveImageView = context->swapchain.images.views[context->image_index];
		}

		vulkan_image_transition(
			context, context->command_buffers[context->current_frame],
			image->handle, VK_IMAGE_ASPECT_COLOR_BIT,
			image->layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		image->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	VkRenderingAttachmentInfo depth_info = { 0 };
	if (pass->depth_attachment.state == VULKAN_RESOURCE_STATE_INITIALIZED) {
		VulkanAttachment *chached = &pass->depth_attachment;
		VulkanImage *image = &context->image_pool[chached->image_index];

		depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_info.imageView = image->view;
		depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depth_info.loadOp = chached->info.loadOp;
		depth_info.storeOp = chached->info.storeOp;
		depth_info.clearValue = chached->info.clearValue;

		vulkan_image_transition(context, context->command_buffers[context->current_frame],
			image->handle, VK_IMAGE_ASPECT_DEPTH_BIT,
			image->layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

		image->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}

	VulkanImage *depth_image = &context->image_pool[pass->depth_attachment.image_index];

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
