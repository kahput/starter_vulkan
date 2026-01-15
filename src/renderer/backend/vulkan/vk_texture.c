#include "common.h"
#include "renderer/r_internal.h"
#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "core/debug.h"
#include "core/logger.h"
#include "core/pool.h"

#include <vulkan/vulkan_core.h>

static VkFormat to_vulkan_format(uint32_t channels, bool is_srgb);

bool vulkan_renderer_texture_create(VulkanContext *context, uint32_t store_index, uint32_t width, uint32_t height, uint32_t channels, bool is_srgb, TextureUsageFlags usage, uint8_t *pixels) {
	if (store_index >= MAX_TEXTURES) {
		LOG_ERROR("Vulkan: texture index %d out of bounds, aborting vulkan_renderer_texture_create", store_index);
		return false;
	}
	if (FLAG_GET(usage, TEXTURE_USAGE_COLOR_ATTACHMENT) && FLAG_GET(usage, TEXTURE_USAGE_DEPTH_ATTACHMENT)) {
		LOG_ERROR("Vulkan: texture cannot be both Color and Depth attachment, aborting vulkan_renderer_texture_create");
		ASSERT(false);
		return false;
	}

	VulkanImage *image = &context->image_pool[store_index];
	if (image->handle != NULL) {
		LOG_FATAL("Vulkan: texture at index %d index is already in use, aborting vulkan_renderer_texture_create", store_index);
		ASSERT(false);
		return false;
	}

	image->width = width, image->height = height;
	uint32_t functional_width = image->width == MATCH_SWAPCHAIN ? context->swapchain.extent.width : width;
	uint32_t functional_height = image->height == MATCH_SWAPCHAIN ? context->swapchain.extent.height : height;

	VkDeviceSize size = functional_width * functional_height * channels;

	VkImageUsageFlags vk_usage = 0;
	VkImageAspectFlags aspect = FLAG_GET(usage, TEXTURE_USAGE_DEPTH_ATTACHMENT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	VkFormat format = to_vulkan_format(channels, is_srgb);
	bool is_attachment = FLAG_GET(usage, TEXTURE_USAGE_COLOR_ATTACHMENT) || FLAG_GET(usage, TEXTURE_USAGE_DEPTH_ATTACHMENT);

	if (is_attachment)
		format = FLAG_GET(usage, TEXTURE_USAGE_DEPTH_ATTACHMENT) ? context->device.depth_format : context->swapchain.format.format;
	else
		vk_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	if (FLAG_GET(usage, TEXTURE_USAGE_SAMPLED))
		vk_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

	if (FLAG_GET(usage, TEXTURE_USAGE_COLOR_ATTACHMENT))
		vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	if (FLAG_GET(usage, TEXTURE_USAGE_DEPTH_ATTACHMENT))
		vk_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	vulkan_image_create(
		context, VK_SAMPLE_COUNT_1_BIT,
		functional_width, functional_height, format, VK_IMAGE_TILING_OPTIMAL,
		vk_usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		image);

	if (pixels) {
		VulkanBuffer *staging_buffer = &context->staging_buffer;
		size_t copy_end = aligned_address(staging_buffer->offset + size, context->device.properties.limits.minMemoryMapAlignment);
		size_t copy_start = staging_buffer->stride * context->current_frame + staging_buffer->offset;
		if (copy_end >= staging_buffer->size) {
			LOG_ERROR("Vulkan: max staging buffer size exceeded, aborting vulkan_renderer_texture_create");
			ASSERT(false);
			return false;
		}

		vulkan_buffer_write_indexed(staging_buffer, context->current_frame, staging_buffer->offset, size, pixels);
		staging_buffer->offset = copy_end;
		vulkan_buffer_to_image(context, copy_start, staging_buffer->handle, image->handle, functional_width, functional_height);
		image->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	if (vulkan_image_view_create(context, aspect, image) == false) {
		LOG_ERROR("Failed to create VkImageView");
		return false;
	}

	LOG_INFO("Vulkan Texture created");
	image->state = VULKAN_RESOURCE_STATE_INITIALIZED;
	return true;
}

bool vulkan_renderer_texture_destroy(VulkanContext *context, uint32_t retrieve_index) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, retrieve_index, MAX_TEXTURES, true);

	vkDestroyImageView(context->device.logical, image->view, NULL);
	vkDestroyImage(context->device.logical, image->handle, NULL);
	vkFreeMemory(context->device.logical, image->memory, NULL);

	*image = (VulkanImage){ 0 };
	return true;
}

bool vvulkan_renderer_texture_prepare_attachment(VulkanContext *context, uint32_t retrieve_index) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, retrieve_index, MAX_TEXTURES, true);

	VkImageLayout new_layout =
		image->aspect == VK_IMAGE_ASPECT_COLOR_BIT
		? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		: VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], new_layout);

	return true;
}
bool vulkan_renderer_texture_prepare_sample(VulkanContext *context, uint32_t retrieve_index) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, retrieve_index, MAX_TEXTURES, true);

	vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	return true;
}

bool vulkan_renderer_texture_resize(VulkanContext *context, uint32_t retrieve_index, uint32_t width, uint32_t height) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, retrieve_index, MAX_TEXTURES, true);

	vkDestroyImageView(context->device.logical, image->view, NULL);
	vkDestroyImage(context->device.logical, image->handle, NULL);
	vkFreeMemory(context->device.logical, image->memory, NULL);

	vulkan_image_create(context, image->info.samples, width, height, image->info.format, VK_IMAGE_TILING_OPTIMAL, image->info.usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image);
	vulkan_image_view_create(context, image->aspect, image);

	return true;
}

bool vulkan_renderer_sampler_create(VulkanContext *context, uint32_t store_index, SamplerDesc description) {
	VulkanSampler *sampler = NULL;
	VULKAN_GET_OR_RETURN(sampler, context->sampler_pool, store_index, MAX_SAMPLERS, false);

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
	sampler->state = VULKAN_RESOURCE_STATE_INITIALIZED;

	return true;
}

bool vulkan_renderer_sampler_destroy(VulkanContext *context, uint32_t retrieve_index) {
	VulkanSampler *sampler = NULL;
	VULKAN_GET_OR_RETURN(sampler, context->sampler_pool, retrieve_index, MAX_SAMPLERS, true);

	vkDestroySampler(context->device.logical, sampler->handle, NULL);
	*sampler = (VulkanSampler){ 0 };

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
