#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "core/debug.h"
#include "core/logger.h"
#include "allocators/pool.h"

#include <vulkan/vulkan_core.h>

static VkFormat to_vulkan_format(uint32_t channels, bool is_srgb);

bool vulkan_renderer_texture_create(VulkanContext *context, uint32_t store_index, uint32_t width, uint32_t height, uint32_t channels, bool is_srgb, uint8_t *pixels) {
	if (store_index >= MAX_TEXTURES) {
		LOG_ERROR("Vulkan: Texture index %d out of bounds", store_index);
		return false;
	}

	VulkanImage *texture = &context->image_pool[store_index];
	if (texture->handle != NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to allocate texture at index %d, but index is already in use", store_index);
		ASSERT(false);
		return false;
	}

	VkDeviceSize size = width * height * channels;

	VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;

	uint32_t indices[] = { context->device.graphics_index, context->device.transfer_index };
	vulkan_buffer_create(context, context->device.graphics_index, size, staging_usage, staging_properties, &staging_buffer, &staging_buffer_memory);

	void *data;
	vkMapMemory(context->device.logical, staging_buffer_memory, 0, size, 0, &data);
	memcpy(data, pixels, (size_t)size);
	vkUnmapMemory(context->device.logical, staging_buffer_memory);

	vulkan_image_create(
		context, indices, countof(indices),
		width, height, to_vulkan_format(channels, is_srgb), VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		texture);

	vulkan_image_transition(
		context, texture->handle, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, VK_ACCESS_TRANSFER_WRITE_BIT);
	vulkan_buffer_to_image(context, staging_buffer, texture->handle, width, height);
	vulkan_image_transition(
		context, texture->handle, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

	vkDestroyBuffer(context->device.logical, staging_buffer, NULL);
	vkFreeMemory(context->device.logical, staging_buffer_memory, NULL);

	if (vulkan_image_view_create(context, texture->handle, texture->format, VK_IMAGE_ASPECT_COLOR_BIT, &texture->view) == false) {
		LOG_ERROR("Failed to create VkImageView");
		return false;
	}

	LOG_INFO("Vulkan Texture created");
	return true;
}

bool vulkan_renderer_texture_destroy(VulkanContext *context, uint32_t retrieve_index) {
	if (retrieve_index >= MAX_TEXTURES) {
		LOG_ERROR("Vulkan: Texture index %d out of bounds", retrieve_index);
		return false;
	}

	VulkanImage *texture = &context->image_pool[retrieve_index];
	if (texture->handle == NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to destroy texture at index %d, but index is not in use", retrieve_index);
		ASSERT(false);
		return false;
	}

	vkDestroyImageView(context->device.logical, texture->view, NULL);
	vkDestroyImage(context->device.logical, texture->handle, NULL);
	vkFreeMemory(context->device.logical, texture->memory, NULL);

	texture->handle = NULL;
	texture->memory = NULL;
	texture->view = NULL;

	return true;
}

bool vulkan_renderer_sampler_create(VulkanContext *context, uint32_t store_index, SamplerDesc description) {
	if (store_index >= MAX_SAMPLERS) {
		LOG_ERROR("Vulkan: Buffer index %d out of bounds", store_index);
		return false;
	}

	VulkanSampler *sampler = &context->sampler_pool[store_index];
	if (sampler->handle != NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to allocatetexture at index %d, but index is already in use", store_index);
		ASSERT(false);
		return false;
	}

	sampler->info = (VkSamplerCreateInfo){
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = (VkFilter)description.mag_filter,
		.minFilter = (VkFilter)description.min_filter,
		.mipmapMode = (VkSamplerMipmapMode)description.mipmap_filter,
		.addressModeU = (VkSamplerAddressMode)description.address_mode_u,
		.addressModeV = (VkSamplerAddressMode)description.address_mode_v,
		.addressModeW = (VkSamplerAddressMode)description.address_mode_w,
		.mipLodBias = 0.0f,
		.anisotropyEnable = description.anisotropy_enable,
		.maxAnisotropy = description.anisotropy_enable ? context->device.properties.limits.maxSamplerAnisotropy : 0.0f,
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.minLod = 0.0f,
		.maxLod = 0.0f,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE
	};

	if (vkCreateSampler(context->device.logical, &sampler->info, NULL, &sampler->handle) != VK_SUCCESS) {
		LOG_ERROR("Failed to create VkSampler");
		return false;
	}

	LOG_INFO("VkSampler created");
	return true;
}

bool vulkan_renderer_sampler_destroy(VulkanContext *context, uint32_t retrieve_index) {
	if (retrieve_index >= MAX_SAMPLERS) {
		LOG_ERROR("Vulkan: Sampler index %d out of bounds", retrieve_index);
		return false;
	}

	VulkanSampler *sampler = &context->sampler_pool[retrieve_index];
	if (sampler->handle == NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to destroy sampler at index %d, but index is not in use", retrieve_index);
		ASSERT(false);
		return false;
	}

	vkDestroySampler(context->device.logical, sampler->handle, NULL);

	sampler->handle = NULL;

	return true;
}

static VkFormat to_vulkan_format(uint32_t channels, bool is_srgb) {
	if (is_srgb)
		switch (channels) {
			case 1:
				return VK_FORMAT_R8_SRGB;
			case 2:
				return VK_FORMAT_R8G8_SRGB;
			case 3:
				return VK_FORMAT_R8G8B8_SRGB;
			case 4:
				return VK_FORMAT_R8G8B8A8_SRGB;
			default: {
				LOG_WARN("Image channels must be in 1-4 range, defaulting to 3");
				return VK_FORMAT_R8G8B8A8_SRGB;
			} break;
		}
	else
		switch (channels) {
			case 1:
				return VK_FORMAT_R8_UNORM;
			case 2:
				return VK_FORMAT_R8G8_UNORM;
			case 3:
				return VK_FORMAT_R8G8B8_UNORM;
			case 4:
				return VK_FORMAT_R8G8B8A8_UNORM;
			default: {
				LOG_WARN("Image channels must be in 1-4 range, defaulting to 3");
				return VK_FORMAT_R8G8B8A8_UNORM;
			} break;
		}
}
