#include "core/logger.h"
#include "renderer/vk_renderer.h"

#include "stb/image.h"
#include <string.h>
#include <vulkan/vulkan_core.h>

void get_filename(const char *src, char *dst) {
	uint32_t start = 0, length = 0;
	char c;

	while ((c = src[length++]) != '\0') {
		if (c == '/' || c == '\\') {
			if (src[length] == '\0') {
				LOG_INFO("'%s' is not a file");
				return;
			}
			start = length;
		}
	}

	memcpy(dst, src + start, length - start);
}

bool vk_create_texture_image(VulkanContext *context) {
	const char *file_path = "assets/textures/container.jpg";
	char file_name[256];
	get_filename(file_path, file_name);
	LOG_INFO("Filepath: %s, Filename: %s", file_path, file_name);
	int32_t width, height, channels;
	uint8_t *pixels = stbi_load(file_path, &width, &height, &channels, STBI_rgb_alpha);

	if (pixels == NULL) {
		LOG_ERROR("Failed to load image [ %s ]", file_name);
	}

	VkDeviceSize size = width * height * 4;

	VkBufferUsageFlags staging_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	VkMemoryPropertyFlags staging_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;

	uint32_t indices[] = { context->device.graphics_index, context->device.transfer_index };
	create_buffer(context, context->device.graphics_index, size, staging_usage, staging_properties, &staging_buffer, &staging_buffer_memory);

	void *data;
	vkMapMemory(context->device.logical, staging_buffer_memory, 0, size, 0, &data);
	memcpy(data, pixels, (size_t)size);
	vkUnmapMemory(context->device.logical, staging_buffer_memory);

	stbi_image_free(pixels);

	vk_image_create(
		context, indices, array_count(indices),
		width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		&context->texture_image, &context->texture_image_memory);

	vk_image_layout_transition(context, context->texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vk_buffer_to_image(context, staging_buffer, context->texture_image, width, height);
	vk_image_layout_transition(context, context->texture_image, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	vkDestroyBuffer(context->device.logical, staging_buffer, NULL);
	vkFreeMemory(context->device.logical, staging_buffer_memory, NULL);

	LOG_INFO("Vulkan Texture created");
	return true;
}

bool vk_create_texture_image_view(VulkanContext *context) {
	VkImageViewCreateInfo image_view_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = context->texture_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.components = {
		  .r = VK_COMPONENT_SWIZZLE_IDENTITY,
		  .g = VK_COMPONENT_SWIZZLE_IDENTITY,
		  .b = VK_COMPONENT_SWIZZLE_IDENTITY,
		  .a = VK_COMPONENT_SWIZZLE_IDENTITY,
		},
		.subresourceRange = {
		  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		  .baseMipLevel = 0,
		  .levelCount = 1,
		  .baseArrayLayer = 0,
		  .layerCount = 1,
		}
	};

	if (vk_image_view_create(context, context->texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, &context->texture_image_view) == false) {
		LOG_ERROR("Failed to create VkImageView");
		return false;
	}

	LOG_INFO("VkImageView created");
	return true;
}

bool vk_create_texture_sampler(VulkanContext *context) {
	VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
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
