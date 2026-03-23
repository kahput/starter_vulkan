#include "core/debug.h"
#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"

VkSampleCountFlags vulkan_utils_max_sample_count(VulkanContext *context) {
	VkPhysicalDeviceLimits limits = context->device.properties.limits;

	VkSampleCountFlags flags = limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts;

	if (flags & VK_SAMPLE_COUNT_64_BIT)
		return VK_SAMPLE_COUNT_64_BIT;
	if (flags & VK_SAMPLE_COUNT_32_BIT)
		return VK_SAMPLE_COUNT_32_BIT;
	if (flags & VK_SAMPLE_COUNT_16_BIT)
		return VK_SAMPLE_COUNT_16_BIT;
	if (flags & VK_SAMPLE_COUNT_8_BIT)
		return VK_SAMPLE_COUNT_8_BIT;
	if (flags & VK_SAMPLE_COUNT_4_BIT)
		return VK_SAMPLE_COUNT_4_BIT;
	if (flags & VK_SAMPLE_COUNT_2_BIT)
		return VK_SAMPLE_COUNT_2_BIT;

	return VK_SAMPLE_COUNT_1_BIT;
}

uint32_t vulkan_utils_type_to_stride(VkFormat format) {
	switch (format) {
		case VK_FORMAT_UNDEFINED:
			return 0;

		// 8-bit R
		case VK_FORMAT_R8_UNORM:
		case VK_FORMAT_R8_SNORM:
		case VK_FORMAT_R8_UINT:
		case VK_FORMAT_R8_SINT:
		case VK_FORMAT_R8_SRGB:
		case VK_FORMAT_S8_UINT:
			return 1;

		// 8-bit RG / 16-bit R
		case VK_FORMAT_R8G8_UNORM:
		case VK_FORMAT_R8G8_SNORM:
		case VK_FORMAT_R8G8_UINT:
		case VK_FORMAT_R8G8_SINT:
		case VK_FORMAT_R8G8_SRGB:
		case VK_FORMAT_R16_UNORM:
		case VK_FORMAT_R16_SNORM:
		case VK_FORMAT_R16_UINT:
		case VK_FORMAT_R16_SINT:
		case VK_FORMAT_R16_SFLOAT:
		case VK_FORMAT_D16_UNORM:
			return 2;

		// 8-bit RGB
		case VK_FORMAT_R8G8B8_UNORM:
		case VK_FORMAT_R8G8B8_SNORM:
		case VK_FORMAT_R8G8B8_UINT:
		case VK_FORMAT_R8G8B8_SINT:
		case VK_FORMAT_R8G8B8_SRGB:
		case VK_FORMAT_B8G8R8_UNORM:
		case VK_FORMAT_B8G8R8_SNORM:
		case VK_FORMAT_B8G8R8_UINT:
		case VK_FORMAT_B8G8R8_SINT:
		case VK_FORMAT_B8G8R8_SRGB:
			return 3;

		// 8-bit RGBA / 16-bit RG / 32-bit R
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SNORM:
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_R8G8B8A8_SINT:
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SNORM:
		case VK_FORMAT_B8G8R8A8_UINT:
		case VK_FORMAT_B8G8R8A8_SINT:
		case VK_FORMAT_B8G8R8A8_SRGB:
		case VK_FORMAT_R16G16_UNORM:
		case VK_FORMAT_R16G16_SNORM:
		case VK_FORMAT_R16G16_UINT:
		case VK_FORMAT_R16G16_SINT:
		case VK_FORMAT_R16G16_SFLOAT:
		case VK_FORMAT_R32_UINT:
		case VK_FORMAT_R32_SINT:
		case VK_FORMAT_R32_SFLOAT:
		case VK_FORMAT_D32_SFLOAT:
			return 4;

		// 16-bit RGB
		case VK_FORMAT_R16G16B16_UNORM:
		case VK_FORMAT_R16G16B16_SNORM:
		case VK_FORMAT_R16G16B16_UINT:
		case VK_FORMAT_R16G16B16_SINT:
		case VK_FORMAT_R16G16B16_SFLOAT:
			return 6;

		// 16-bit RGBA / 32-bit RG / 64-bit R
		case VK_FORMAT_R16G16B16A16_UNORM:
		case VK_FORMAT_R16G16B16A16_SNORM:
		case VK_FORMAT_R16G16B16A16_UINT:
		case VK_FORMAT_R16G16B16A16_SINT:
		case VK_FORMAT_R16G16B16A16_SFLOAT:
		case VK_FORMAT_R32G32_UINT:
		case VK_FORMAT_R32G32_SINT:
		case VK_FORMAT_R32G32_SFLOAT:
		case VK_FORMAT_R64_UINT:
		case VK_FORMAT_R64_SINT:
		case VK_FORMAT_R64_SFLOAT:
			return 8;

		// 32-bit RGB
		case VK_FORMAT_R32G32B32_UINT:
		case VK_FORMAT_R32G32B32_SINT:
		case VK_FORMAT_R32G32B32_SFLOAT:
			return 12;

		// 32-bit RGBA / 64-bit RG
		case VK_FORMAT_R32G32B32A32_UINT:
		case VK_FORMAT_R32G32B32A32_SINT:
		case VK_FORMAT_R32G32B32A32_SFLOAT:
		case VK_FORMAT_R64G64_UINT:
		case VK_FORMAT_R64G64_SINT:
		case VK_FORMAT_R64G64_SFLOAT:
			return 16;

		// 64-bit RGB
		case VK_FORMAT_R64G64B64_UINT:
		case VK_FORMAT_R64G64B64_SINT:
		case VK_FORMAT_R64G64B64_SFLOAT:
			return 24;

		// 64-bit RGBA
		case VK_FORMAT_R64G64B64A64_UINT:
		case VK_FORMAT_R64G64B64A64_SINT:
		case VK_FORMAT_R64G64B64A64_SFLOAT:
			return 32;

		default:
			ASSERT(false);
			return 0;
	}
}

VkFormat vulkan_utils_to_vkformat(VulkanContext *context, TextureFormat format) {
	switch (format) {
		case TEXTURE_FORMAT_RGBA8:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case TEXTURE_FORMAT_RGBA16F:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case TEXTURE_FORMAT_RGBA32F:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
		case TEXTURE_FORMAT_R8:
			return VK_FORMAT_R8_UNORM;
		case TEXTURE_FORMAT_DEPTH:
			return context->device.depth_format;
		case TEXTURE_FORMAT_RGBA8_SRGB:
			return VK_FORMAT_R8G8B8A8_SRGB;
		default:
			ASSERT(false);
			return VK_FORMAT_UNDEFINED;
	}
}
