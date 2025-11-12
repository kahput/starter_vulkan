#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_framebuffers(VulkanContext *context) {
	context->swapchain.framebuffer_count = context->swapchain.image_count;

	for (uint32_t i = 0; i < context->swapchain.framebuffer_count; ++i) {
		VkImageView attachments[] = {
			context->swapchain.image_views[i],
			context->depth_image_view
		};

		VkFramebufferCreateInfo fb_create_info = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = context->render_pass,
			.attachmentCount = array_count(attachments),
			.pAttachments = attachments,
			.width = context->swapchain.extent.width,
			.height = context->swapchain.extent.height,
			.layers = 1
		};

		if (vkCreateFramebuffer(context->device.logical, &fb_create_info, NULL, context->swapchain.framebuffers + i) != VK_SUCCESS) {
			LOG_ERROR("Failed to create framebuffer");
			return false;
		}
	}

	LOG_INFO("Vulkan Framebuffers created");

	return true;
}
