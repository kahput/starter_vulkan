#include "common.h"
#include "core/pool.h"
#include "core/r_types.h"
#include "renderer/backend/vulkan_api.h"
#include "renderer/r_internal.h"
#include "vk_internal.h"

#include "core/debug.h"
#include "core/logger.h"

#include <vulkan/vulkan_core.h>

static VkFormat to_vulkan_format(VulkanContext *context, TextureFormat format);
static VkImageAspectFlags to_aspect(TextureFormat format);
static uint32_t to_stride(TextureFormat format);
static VkImageViewType to_view_type(TextureType type);
static VkImageUsageFlags to_usage_flags(TextureFormat format, TextureUsageFlags usage,
	bool has_pixels);

RhiTexture vulkan_renderer_texture_create(
	VulkanContext *context,
	uint32_t width, uint32_t height,
	TextureType type, TextureFormat format, TextureUsageFlags usage,
	uint8_t *pixels) {
	VulkanImage *image = pool_alloc_struct(context->image_pool, VulkanImage);

	uint32_t layer_count = type == TEXTURE_TYPE_CUBE ? 6 : 1;

	VkDeviceSize layer_size = width * height * to_stride(format);
	VkDeviceSize total_size = layer_size * layer_count;

	VkImageUsageFlags vk_usage = to_usage_flags(format, usage, pixels != NULL);
	VkFormat vk_format = to_vulkan_format(context, format);
	VkImageAspectFlags aspect = to_aspect(format);

	vulkan_image_create(context, VK_SAMPLE_COUNT_1_BIT, width, height, vk_format,
		VK_IMAGE_TILING_OPTIMAL, vk_usage, type, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		image);

	if (pixels) {
		VulkanBuffer *staging_buffer = &context->staging_buffer;
		size_t copy_end = aligned_address(staging_buffer->offset + total_size,
			context->device.properties.limits.minMemoryMapAlignment);
		size_t copy_start = staging_buffer->stride * context->current_frame + staging_buffer->offset;
		if (copy_end >= staging_buffer->size) {
			LOG_ERROR(
				"Vulkan: max staging buffer size exceeded, aborting vulkan_renderer_texture_create");
			ASSERT(false);
			return INVALID_RHI(RhiTexture);
		}

		vulkan_buffer_write_indexed(staging_buffer, context->current_frame, staging_buffer->offset,
			total_size, pixels);
		staging_buffer->offset = copy_end;
		vulkan_buffer_to_image(context, copy_start, staging_buffer->handle, image->handle, width,
			height, layer_count, layer_size);
		image->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	if (vulkan_image_view_create(context, to_view_type(type), aspect, image) == false) {
		LOG_ERROR("Failed to create VkImageView");
		return INVALID_RHI(RhiTexture);
	}

	LOG_INFO("Vulkan Texture created");
	image->state = VULKAN_RESOURCE_STATE_INITIALIZED;
	return (RhiTexture){ pool_index_of(context->image_pool, image) };
}

bool vulkan_renderer_texture_destroy(VulkanContext *context, RhiTexture texture) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, texture, MAX_TEXTURES, true, false);

	vkDestroyImageView(context->device.logical, image->view, NULL);
	vkDestroyImage(context->device.logical, image->handle, NULL);
	vkFreeMemory(context->device.logical, image->memory, NULL);

	*image = (VulkanImage){ 0 };

	pool_free(context->image_pool, image);

	return true;
}

bool vvulkan_renderer_texture_prepare_attachment(VulkanContext *context, RhiTexture texture) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, texture, MAX_TEXTURES, true, false);

	VkImageLayout new_layout = image->aspect == VK_IMAGE_ASPECT_COLOR_BIT
		? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		: VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], new_layout);

	return true;
}
bool vulkan_renderer_texture_prepare_sample(VulkanContext *context, RhiTexture texture) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, texture, MAX_TEXTURES, true, false);

	VkImageLayout new_layout = image->aspect == VK_IMAGE_ASPECT_COLOR_BIT
		? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		: VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

	vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], new_layout);

	return true;
}

bool vulkan_renderer_texture_resize(VulkanContext *context, RhiTexture texture, uint32_t width,
	uint32_t height) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, texture, MAX_TEXTURES, true, false);

	vkDestroyImageView(context->device.logical, image->view, NULL);
	vkDestroyImage(context->device.logical, image->handle, NULL);
	vkFreeMemory(context->device.logical, image->memory, NULL);

	vulkan_image_create(context, image->info.samples, width, height, image->info.format,
		VK_IMAGE_TILING_OPTIMAL, image->info.usage, image->type,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image);
	vulkan_image_view_create(context, to_view_type(image->type), image->aspect, image);

	return true;
}

RhiSampler vulkan_renderer_sampler_create(VulkanContext *context, SamplerDesc description) {
	VulkanSampler *sampler = pool_alloc(context->sampler_pool);

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
		.maxAnisotropy = description.anisotropy_enable
			? context->device.properties.limits.maxSamplerAnisotropy
			: 0.0f,
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.minLod = 0.0f,
		.maxLod = 0.0f,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE
	};

	if (vkCreateSampler(context->device.logical, &sampler->info, NULL, &sampler->handle) !=
		VK_SUCCESS) {
		LOG_ERROR("Failed to create VkSampler");
		return INVALID_RHI(RhiSampler);
	}

	LOG_INFO("VkSampler created");
	sampler->state = VULKAN_RESOURCE_STATE_INITIALIZED;

	return (RhiSampler){ pool_index_of(context->sampler_pool, sampler) };
}

bool vulkan_renderer_sampler_destroy(VulkanContext *context, RhiSampler handle) {
	VulkanSampler *sampler = NULL;
	VULKAN_GET_OR_RETURN(sampler, context->sampler_pool, handle, MAX_SAMPLERS, true, false);

	vkDestroySampler(context->device.logical, sampler->handle, NULL);
	*sampler = (VulkanSampler){ 0 };

	pool_free(context->sampler_pool, sampler);

	return true;
}

VkFormat to_vulkan_format(VulkanContext *context, TextureFormat format) {
	switch (format) {
		case TEXTURE_FORMAT_RGBA8:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case TEXTURE_FORMAT_RGBA16F:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
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

uint32_t to_stride(TextureFormat format) {
	switch (format) {
		case TEXTURE_FORMAT_RGBA8:
			return 4;
		case TEXTURE_FORMAT_RGBA16F:
			return 8;
		case TEXTURE_FORMAT_R8:
			return 1;
		case TEXTURE_FORMAT_DEPTH:
			return 4;
		default:
			return 4;
	}
}

VkImageAspectFlags to_aspect(TextureFormat format) {
	switch (format) {
		case TEXTURE_FORMAT_DEPTH:
			return VK_IMAGE_ASPECT_DEPTH_BIT;
		case TEXTURE_FORMAT_DEPTH_STENCIL:
			return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		default:
			return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

VkImageViewType to_view_type(TextureType type) {
	switch (type) {
		case TEXTURE_TYPE_1D:
			return VK_IMAGE_VIEW_TYPE_1D;
		case TEXTURE_TYPE_2D:
			return VK_IMAGE_VIEW_TYPE_2D;
		case TEXTURE_TYPE_3D:
			return VK_IMAGE_VIEW_TYPE_3D;
		case TEXTURE_TYPE_CUBE:
			return VK_IMAGE_VIEW_TYPE_CUBE;
		default:
			ASSERT(false);
			return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	}
}

VkImageUsageFlags to_usage_flags(TextureFormat format, TextureUsageFlags usage, bool has_pixels) {
	VkImageUsageFlags vk_usage = 0;

	if (has_pixels)
		vk_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	if (FLAG_GET(usage, TEXTURE_USAGE_SAMPLED))
		vk_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

	if (FLAG_GET(usage, TEXTURE_USAGE_RENDER_TARGET)) {
		if (format == TEXTURE_FORMAT_DEPTH || format == TEXTURE_FORMAT_DEPTH_STENCIL)
			vk_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		else
			vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	if (vk_usage == 0) {
		LOG_ERROR("Vulkan: texture created with neither SAMPLED nor RENDER_TARGET usage");
		ASSERT(false);
		return 0;
	}

	return vk_usage;
}
