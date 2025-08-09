#pragma once

#include "core/arena.h"
#include "core/logger.h"

#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <vulkan/vulkan_core.h>


typedef struct {
	VkInstance instance;

	VkPhysicalDevice physical_device;
	VkDevice logical_device;
	VkQueue graphics_queue, present_queue;

	VkSurfaceKHR surface;

#ifndef NDEBUG
	VkDebugUtilsMessengerEXT debug_messenger;
#endif
} VulkanRenderer;

typedef struct platform Platform;

bool vk_create_instance(Arena *arena, VulkanRenderer *renderer);
void vk_load_extensions(VulkanRenderer *renderer);
bool vk_create_surface(Platform *platform, VulkanRenderer *renderer);
bool vk_create_device(Arena *arena, VulkanRenderer *renderer);

// EXTENSIONS
#define VK_CREATE_UTIL_DEBUG_MESSENGER(name) \
	VKAPI_ATTR VkResult VKAPI_CALL name(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pMessenger)
VK_CREATE_UTIL_DEBUG_MESSENGER(vk_create_utils_debug_messneger_default);

typedef VK_CREATE_UTIL_DEBUG_MESSENGER(fn_create_utils_debug_messenger);
static fn_create_utils_debug_messenger *vkCreateDebugUtilsMessenger = vk_create_utils_debug_messneger_default;
