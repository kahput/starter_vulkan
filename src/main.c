#include "core/logger.h"

#include "vk_renderer.h"

#include <stdbool.h>
#include <vulkan/vulkan_core.h>

int main(void) {
	Arena *vk_arena = arena_alloc();

	uint32_t version = 0;
	vkEnumerateInstanceVersion(&version);
	logger_set_level(LOG_LEVEL_INFO);

	LOG_INFO("Vulkan %d.%d.%d", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));

	VulkanRenderer renderer = { 0 };
	vk_create_instance(vk_arena, &renderer);
	vk_load_extensions(&renderer);
	vk_select_physical_device(vk_arena, &renderer);

	return 0;
}
