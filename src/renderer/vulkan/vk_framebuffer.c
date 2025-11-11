#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_framebuffers(VulkanContext *ctx) {
	ctx->swapchain.framebuffer_count = ctx->swapchain.image_count;

	for (uint32_t i = 0; i < ctx->swapchain.framebuffer_count; ++i) {
		VkImageView attachments[] = {
			ctx->swapchain.image_views[i],
			ctx->depth_image_view
		};

		VkFramebufferCreateInfo fb_create_info = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = ctx->render_pass,
			.attachmentCount = array_count(attachments),
			.pAttachments = attachments,
			.width = ctx->swapchain.extent.width,
			.height = ctx->swapchain.extent.height,
			.layers = 1
		};

		if (vkCreateFramebuffer(ctx->device.logical, &fb_create_info, NULL, ctx->swapchain.framebuffers + i) != VK_SUCCESS) {
			LOG_ERROR("Failed to create framebuffer");
			return false;
		}
	}

	LOG_INFO("Vulkan Framebuffers created");

	return true;
}
