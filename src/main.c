#include "core/arena.h"
#include "core/logger.h"

#include "platform.h"
#include "vk_renderer.h"

#include <stdbool.h>
#include <vulkan/vulkan_core.h>

int main(void) {
	Arena *vk_arena = arena_alloc();
	Arena *window_arena = arena_alloc();

	uint32_t version = 0;
	vkEnumerateInstanceVersion(&version);
	logger_set_level(LOG_LEVEL_INFO);

	LOG_INFO("Vulkan %d.%d.%d", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));

	VulkanRenderer renderer = { 0 };
	Platform *platform = platform_startup(window_arena, 1280, 720, "Starter Vulkan");
	if (platform == NULL) {
		LOG_ERROR("Platform startup failed");
		return -1;
	}
	LOG_INFO("Successfully created wayland display");
	vk_create_instance(vk_arena, &renderer);
	vk_load_extensions(&renderer);
	vk_create_surface(platform, &renderer);
	vk_select_physical_device(vk_arena, &renderer);
	vk_create_logical_device(vk_arena, &renderer);

	while (platform_should_close(platform) == false) {
		platform_poll_events(platform);
	}

	return 0;
}
