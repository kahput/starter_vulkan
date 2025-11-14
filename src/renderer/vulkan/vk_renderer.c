#include "renderer/vk_renderer.h"
#include "core/logger.h"

bool vulkan_renderer_create(struct arena *arena, struct platform *platform, VulkanContext *context) {
	if (vulkan_create_instance(context, platform) == false)
		return false;

	if (vulkan_create_surface(platform, context) == false)
		return false;

	if (vulkan_create_device(arena, context) == false)
		return false;

	if (vulkan_create_command_pool(context) == false)
		return false;

	if (vulkan_create_command_buffer(context) == false)
		return false;

	if (vulkan_create_sync_objects(context) == false)
		return false;

	if (vulkan_create_swapchain(context, platform) == false)
		return false;

	if (vulkan_create_depth_image(context) == false)
		return false;

	return true;
}
void vulkan_renderer_destroy(VulkanContext *context) {
}
