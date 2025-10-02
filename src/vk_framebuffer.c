#include "vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

bool vk_create_framebuffers(struct arena *arena, VKRenderer *renderer) {
	uint32_t offset = arena_size(arena);
	renderer->framebuffer_count = renderer->swapchain.image_count;
	renderer->framebuffers = arena_push_array_zero(arena, VkFramebuffer, renderer->framebuffer_count);

	for (uint32_t i = 0; i < renderer->framebuffer_count; ++i) {
		VkImageView attachments[] = {
			renderer->image_views[i]
		};

		VkFramebufferCreateInfo fb_create_info = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = renderer->render_pass,
			.attachmentCount = array_count(attachments),
			.pAttachments = attachments,
			.width = renderer->swapchain.extent.width,
			.height = renderer->swapchain.extent.height,
			.layers = 1
		};

		if (vkCreateFramebuffer(renderer->logical_device, &fb_create_info, NULL, renderer->framebuffers + i) != VK_SUCCESS) {
			LOG_ERROR("Failed to create framebuffer");
			arena_set(arena, offset);
			return false;
		}
	}

	LOG_INFO("Vulkan Framebuffers created");

	return true;
}
