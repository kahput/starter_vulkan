#pragma once

#include "core/debug.h"
#include "core/arena.h"
#include "core/logger.h"

#include "os.h"

#include "gfx.h"
#include <vulkan/vulkan.h>

struct GFX_Device {
	VkInstance instance;
	VkDevice device;
};

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
	VkDebugUtilsMessageTypeFlagsEXT message_type,
	const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
	void *pUserData);
