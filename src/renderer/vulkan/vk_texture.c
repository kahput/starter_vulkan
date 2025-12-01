#include "allocators/pool.h"
#include "renderer/vk_renderer.h"

#include "core/logger.h"

#include <string.h>
#include <vulkan/vulkan_core.h>

static VkFormat channels_to_vulkan_format(uint32_t channels);

bool vulkan_renderer_create_texture(VulkanContext *context, uint32_t store_index, const Image *image) {
	VkDeviceSize size = image->width * image->height * image->channels;

	if (store_index >= MAX_TEXTURES) {
		LOG_ERROR("Vulkan: Buffer index %d out of bounds", store_index);
		return false;
	}

	VulkanImage *texture = &context->texture_pool[store_index];
	if (texture->handle != NULL) {
		LOG_FATAL("Engine: Frontend renderer allocated texture at index %d, but index is already in use", store_index);
		assert(false);
		return false;
	}

	VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;

	uint32_t indices[] = { context->device.graphics_index, context->device.transfer_index };
	vulkan_create_buffer(context, context->device.graphics_index, size, staging_usage, staging_properties, &staging_buffer, &staging_buffer_memory);

	void *data;
	vkMapMemory(context->device.logical, staging_buffer_memory, 0, size, 0, &data);
	memcpy(data, image->pixels, (size_t)size);
	vkUnmapMemory(context->device.logical, staging_buffer_memory);

	vulkan_image_create(
		context, indices, array_count(indices),
		image->width, image->height, channels_to_vulkan_format(image->channels), VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		texture);

	vulkan_image_transition(
		context, texture->handle, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, VK_ACCESS_TRANSFER_WRITE_BIT);
	vulkan_buffer_to_image(context, staging_buffer, texture->handle, image->width, image->height);
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

bool vulkan_renderer_create_sampler(VulkanContext *context) {
	VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.mipLodBias = 0.0f,
		.anisotropyEnable = true,
		.maxAnisotropy = context->device.properties.limits.maxSamplerAnisotropy,
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.minLod = 0.0f,
		.maxLod = 0.0f,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE
	};

	if (vkCreateSampler(context->device.logical, &sampler_info, NULL, &context->texture_sampler) != VK_SUCCESS) {
		LOG_ERROR("Failed to create VkSampler");
		return false;
	}

	LOG_INFO("VkSampler created");
	return true;
}

static VkFormat channels_to_vulkan_format(uint32_t channels) {
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
}
