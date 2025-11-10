#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_framebuffers(VulkanState *vk_state) {
	vk_state->swapchain.framebuffer_count = vk_state->swapchain.image_count;

	for (uint32_t i = 0; i < vk_state->swapchain.framebuffer_count; ++i) {
		VkImageView attachments[] = {
			vk_state->swapchain.image_views[i],
			vk_state->depth_image_view
		};

		VkFramebufferCreateInfo fb_create_info = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = vk_state->render_pass,
			.attachmentCount = array_count(attachments),
			.pAttachments = attachments,
			.width = vk_state->swapchain.extent.width,
			.height = vk_state->swapchain.extent.height,
			.layers = 1
		};

		if (vkCreateFramebuffer(vk_state->device.logical, &fb_create_info, NULL, vk_state->swapchain.framebuffers + i) != VK_SUCCESS) {
			LOG_ERROR("Failed to create framebuffer");
			return false;
		}
	}

	LOG_INFO("Vulkan Framebuffers created");

	return true;
}
