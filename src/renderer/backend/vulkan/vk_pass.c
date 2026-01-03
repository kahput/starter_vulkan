#include "vk_internal.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "common.h"
#include "core/debug.h"
#include "core/logger.h"

static bool check_global_resource(VulkanContext *context, uint32_t global_resource, uint32_t count, PassSampleDesc *samples);
static bool create_attachment(VulkanContext *context, VulkanAttachment *attachment, uint32_t width, uint32_t height, bool depth, AttachmentDesc *desc);

bool vulkan_renderer_pass_create(VulkanContext *context, uint32_t store_index, uint32_t global_resource, RenderPassDesc *desc) {
	if (store_index >= MAX_RENDER_PASSES) {
		LOG_ERROR("Vulkan: pass index %d out of bounds, aborting vulkan_renderer_pass_create", store_index);
		return false;
	}

	VulkanPass *pass = &context->pass_pool[store_index];
	if (pass->color_attachments[0].image.handle != NULL && pass->depth_attachment.image.handle != NULL) {
		LOG_ERROR("Vulkan: pass at index %d is already in use, aborting vulkan_renderer_pass_create", store_index);
		ASSERT(false);
		return false;
	}

	// TODO: validate global resource
	(void)check_global_resource(context, global_resource, desc->sample_count, desc->samples);
	// if (check_global_resource(context, global_resource, desc->sample_count, desc->samples))
	// 	return false;

	uint32_t width = desc->width, height = desc->height;
	pass->width = width, pass->height = height;
	memcpy(&pass->desc, desc, sizeof(RenderPassDesc));
	if (width == 0 || height == 0) {
		width = context->swapchain.extent.width;
		height = context->swapchain.extent.height;
	}

	for (uint32_t index = 0; index < desc->color_count; ++index) {
		pass->color_attachment_count++;
		AttachmentDesc *attachment_desc = &desc->color_attachments[index];
		VulkanAttachment *attachment = &pass->color_attachments[index];

		create_attachment(context, attachment, width, height, false, attachment_desc);
	}

	if (desc->depth_attachment.source == RENDER_TARGET_SOURCE_UNUSED)
		return true;
	create_attachment(context, &pass->depth_attachment, width, height, true, &desc->depth_attachment);

	return true;
}

void vulkan_renderer_pass_destroy(VulkanContext *context, uint32_t retrieve_index) {
	if (retrieve_index >= MAX_RENDER_PASSES)
		return;

	VulkanPass *pass = &context->pass_pool[retrieve_index];
	if (pass->color_attachments[0].image.handle == NULL && pass->depth_attachment.image.handle == NULL)
		return;

	for (uint32_t index = 0; index < pass->color_attachment_count; ++index) {
		VulkanImage *image = &pass->color_attachments[index].image;

		if (image->handle) {
			vkDestroyImageView(context->device.logical, image->view, NULL);
			vkDestroyImage(context->device.logical, image->handle, NULL);
			vkFreeMemory(context->device.logical, image->memory, NULL);

			image->handle = NULL;
			image->memory = NULL;
			image->view = NULL;
		}
	}

	VulkanImage *image = &pass->depth_attachment.image;
	if (image->handle) {
		vkDestroyImageView(context->device.logical, image->view, NULL);
		vkDestroyImage(context->device.logical, image->handle, NULL);
		vkFreeMemory(context->device.logical, image->memory, NULL);

		image->handle = NULL;
		image->memory = NULL;
		image->view = NULL;
	}

	memset(pass, 0, sizeof(VulkanPass));
}

bool vulkan_renderer_pass_begin(VulkanContext *context, uint32_t retrieve_index) {
	if (retrieve_index >= MAX_RENDER_PASSES) {
		LOG_ERROR("Vulkan: pass index %d out of bounds, aborting vulkan_renderer_pass_begin", retrieve_index);
		return false;
	}

	VulkanPass *pass = &context->pass_pool[retrieve_index];
	if (pass->color_attachments[0].image.handle == NULL && pass->depth_attachment.image.handle == NULL) {
		LOG_ERROR("Vulkan: pass at index %d is not in use, aborting vulkan_renderer_pass_begin", retrieve_index);
		ASSERT(false);
		return false;
	}

	VkExtent2D extent = {
		.width = pass->width == 0 ? context->swapchain.extent.width : pass->width,
		.height = pass->height == 0 ? context->swapchain.extent.height : pass->height
	};

	VkRenderingAttachmentInfo color_attachments[4] = { 0 };
	for (uint32_t color_index = 0; color_index < pass->color_attachment_count; ++color_index) {
		VulkanImage *color_image = &pass->color_attachments[color_index].image;
		VkImageView view = color_image->view;

		VulkanAttachment *attachment = &pass->color_attachments[color_index];
		color_attachments[color_index] = (VkRenderingAttachmentInfo){
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = view,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.loadOp = attachment->load_op,
			.storeOp = attachment->store_op,
			.clearValue = attachment->clear,
		};

		if (pass->color_attachments[color_index].present) {
			color_attachments[0].resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			color_attachments[0].resolveImageView = context->swapchain.images.views[context->image_index];
			color_attachments[0].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
	}

	VkRenderingAttachmentInfo depth_attachment = { 0 };
	VulkanImage *depth_image = &pass->depth_attachment.image;
	if (depth_image->handle != NULL) {
		VulkanAttachment *attachment = &pass->depth_attachment;

		depth_attachment = (VkRenderingAttachmentInfo){
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = depth_image->view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.loadOp = attachment->load_op,
			.storeOp = attachment->store_op,
			.clearValue = attachment->clear,
		};
	}

	VkRenderingInfo pass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {
		  .offset = { 0, 0 },
		  .extent = extent,
		},
		.layerCount = 1,
		.colorAttachmentCount = pass->color_attachment_count,
		.pColorAttachments = color_attachments,
		.pDepthAttachment = depth_attachment.imageView ? &depth_attachment : NULL,
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

bool vulkan_pass_on_resize(VulkanContext *context, VulkanPass *pass) {
	if (pass->color_attachments[0].image.handle == NULL && pass->depth_attachment.image.handle == NULL)
		return false;

	bool present = false;
	for (uint32_t color_index = 0; color_index < pass->color_attachment_count; ++color_index) {
		VulkanAttachment *attachment = &pass->color_attachments[color_index];

		if (attachment->present) {
			present = true;
			break;
		}
	}

	if (present) {
		uint32_t index = pass - context->pass_pool;
		uint32_t global_resource = pass->global_resource;
		RenderPassDesc desc = pass->desc;

		vulkan_renderer_pass_destroy(context, index);
		vulkan_renderer_pass_create(context, index, global_resource, &desc);
		return true;
	}

	return false;
}

bool create_attachment(VulkanContext *context, VulkanAttachment *attachment, uint32_t width, uint32_t height, bool depth, AttachmentDesc *desc) {
	VkFormat format = depth ? VK_FORMAT_D32_SFLOAT : context->swapchain.format.format;
	VkImageAspectFlags aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	VkImageUsageFlags usage = depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	VkImageLayout layout = depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	attachment->load_op = (int32_t)desc->load_op;
	attachment->store_op = (int32_t)desc->store_op;
	attachment->clear = (VkClearValue){ .color = { { desc->clear.color[0], desc->clear.color[1], desc->clear.color[2], desc->clear.color[3] } } };
	attachment->present = desc->present;

	if (desc->source == RENDER_TARGET_SOURCE_PASS_OUTPUT) {
		VulkanPass *src_pass = &context->pass_pool[desc->source_info.index];
		if (format == VK_FORMAT_D32_SFLOAT) {
			VulkanAttachment *source_attachment = &src_pass->depth_attachment;
			if (source_attachment->image.handle == NULL) {
				LOG_ERROR("Vulkan: invalid source depth attachment, creating new");
			} else {
				memcpy(attachment, source_attachment, sizeof(VulkanAttachment));
				return true;
			}
		} else {
			VulkanAttachment *source_attachment = &src_pass->color_attachments[desc->source_info.output];
			if (source_attachment->image.handle == NULL) {
				LOG_ERROR("Vulkan: invalid source color attachment, creating new");
			} else {
				memcpy(attachment, source_attachment, sizeof(VulkanAttachment));
				return true;
			}
		}
	}
	VulkanImage *image = &attachment->image;
	VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	vulkan_image_create(
		context, context->sample_count, width, height,
		format, VK_IMAGE_TILING_OPTIMAL, usage, memory_properties,
		image);
	vulkan_image_view_create(context, image->handle, format, aspect, &image->view);
	vulkan_image_transition_oneshot(
		context, image->handle, aspect,
		VK_IMAGE_LAYOUT_UNDEFINED, layout,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0, 0);

	return true;
}

static bool check_global_resource(VulkanContext *context, uint32_t global_resource, uint32_t count, PassSampleDesc *samples) {
	if (global_resource >= MAX_GLOBAL_RESOURCES) {
		LOG_ERROR("Vulkan: global resource index %d out of bounds, aborting vulkan_renderer_pass_create", global_resource);
		return false;
	}

	VulkanGlobalResource *global = &context->global_resources[global_resource];
	if (global->set == NULL) {
		LOG_ERROR("Vulkan: global resource at index %d not in use, aborting vulkan_renderer_pass_create", global_resource);
		ASSERT(false);
		return false;
	}
	VulkanBuffer *buffer = &global->buffer;

	static const char *stringify[] = {
		"UNIFORM_BUFFER",
		"STORAGE_BUFFER",
		"TEXTURE_SAMPLER",
		"TEXTURE",
		"SAMPLEr"
	};

	bool valid = true;
	for (uint32_t sample_index = 0; sample_index < count; ++sample_index) {
		PassSampleDesc *sample = &samples[sample_index];

		bool sample_found = false;
		for (uint32_t binding_index = 0; binding_index < global->binding_count; ++binding_index) {
			// TODO: Multiple bindings for global resources
		}
		if (sample_found == false)
			return false;
		valid = sample_found;
	}

	return valid;
}
