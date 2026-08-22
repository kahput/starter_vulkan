#include "common.h"
#include "core/pool.h"
#include "core/r_types.h"
#include "renderer/backend/vulkan_api.h"
#include "renderer/r_internal.h"
#include "vk_internal.h"

#include "core/debug.h"
#include "core/logger.h"

#include <string.h>
#include <vulkan/vulkan_core.h>

static VkImageAspectFlags to_aspect(ImageFormat format);
static VkImageViewType to_view_type(ImageType type);
static VkImageUsageFlags to_usage_flags(ImageFormat format, ImageUsageFlags usage,
	bool has_pixels);

RhiImage vulkan_image_make(
	VulkanContext *context,
	uint32_t width, uint32_t height,
	ImageType type, ImageFormat format, ImageUsageFlags usage,
	void *data) {
	uint8_t *pixels = data;
	VulkanImage *image = pool_alloc_struct(context->image_pool, VulkanImage);

	uint32_t layer_count = type == IMAGE_TYPE_CUBE ? 6 : 1;

	ASSERT_MESSAGE(!(pixels == NULL && FLAG_GET(usage, IMAGE_USAGE_RENDER_TARGET) == false && FLAG_GET(usage, IMAGE_USAGE_SAMPLED)), "NOTE: This means transfer destination isn't set");
	VkImageUsageFlags vk_usage = to_usage_flags(format, usage, pixels != NULL);
	VkFormat vk_format = vulkan_utils_to_vkformat(context, format);
	VkImageAspectFlags aspect = to_aspect(format);

	VkDeviceSize layer_size = width * height * vulkan_utils_format_to_stride(vk_format);
	VkDeviceSize total_size = layer_size * layer_count;

	_vulkan_image_make(context, VK_SAMPLE_COUNT_1_BIT, width, height, vk_format,
		VK_IMAGE_TILING_OPTIMAL, vk_usage, type, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		image);

	if (pixels)
		_vulkan_image_upload(context, pixels, image);

	if (_vulkan_imageview_make(context, to_view_type(type), aspect, image) == false) {
		LOG_ERROR("Failed to create VkImageView");
		return INVALID_RHI(RhiImage);
	}

	LOG_INFO("image[ID = %d] loaded successfuly (%ux%u | %s)", indexof(context->image_pool, image), width, height, image_format_to_string[format]);
	image->state = VULKAN_RESOURCE_STATE_INITIALIZED;
	return (RhiImage){ indexof(context->image_pool, image) };
}

bool vulkan_image_destroy(VulkanContext *context, RhiImage image_handle) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, image_handle, MAX_IMAGES, true, false);

	vkDestroyImageView(context->device.logical, image->view, NULL);
	vkDestroyImage(context->device.logical, image->handle, NULL);
	vkFreeMemory(context->device.logical, image->memory, NULL);

	*image = (VulkanImage){ 0 };

	pool_free(context->image_pool, image);

	return true;
}

bool vulkan_image_read_pixel(VulkanContext *context, RhiImage image_handle, uint32_t x, uint32_t y, void *pixel) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, image_handle, MAX_IMAGES, true, false);

	if (x > image->width || y > image->height)
		return false;

	VkImageLayout cached = image->layout;

	VkCommandBuffer command_buffer;
	vulkan_command_oneshot_begin(context, context->graphics_command_pool, &command_buffer);

	ASSERT_MESSAGE(image->info.arrayLayers == 1, "Reading from image with multiple layers unsupported");

	VulkanBuffer *buffer = &context->staging_buffer;

	_vulkan_image_transition_auto(image, command_buffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	_vulkan_image_to_buffer(context, command_buffer, image, buffer, x, y, 1, 1);

	if (cached != VK_IMAGE_LAYOUT_UNDEFINED)
		_vulkan_image_transition_auto(image, command_buffer, cached);

	vulkan_command_oneshot_end(context, context->device.graphics_queue, context->graphics_command_pool, &command_buffer);

	// NOTE: Async risk, as staging buffer offset wasn't bumped
	memory_copy(
		pixel,
		(uint8_t *)buffer->mapped + buffer->frame_size * context->current_frame + buffer->offset,
		vulkan_utils_format_to_stride(image->info.format));
	return true;
}
bool vulkan_image_read_pixels(VulkanContext *context, RhiImage image_handle, uint32_t x, uint32_t y, void *pixels) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, image_handle, MAX_IMAGES, true, false);

	if (x > image->width || y > image->height)
		return false;

	VulkanBuffer *buffer = &context->staging_buffer;
	if (buffer->offset + vulkan_utils_format_to_stride(image->info.format) * image->width * image->height > buffer->frame_size)
		return false;

	VkImageLayout cached = image->layout;

	VkCommandBuffer command_buffer;
	vulkan_command_oneshot_begin(context, context->graphics_command_pool, &command_buffer);

	ASSERT_MESSAGE(image->info.arrayLayers == 1, "Reading from image with multiple layers unsupported");

	_vulkan_image_transition_auto(image, command_buffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	_vulkan_image_to_buffer(context, command_buffer, image, buffer, x, y, image->width, image->height);
	_vulkan_image_transition_auto(image, command_buffer, cached);

	vulkan_command_oneshot_end(context, context->device.graphics_queue, context->graphics_command_pool, &command_buffer);

	// NOTE: Async risk, as staging buffer offset wasn't bumped
	memory_copy(
		pixels,
		(uint8_t *)buffer->mapped + buffer->frame_size * context->current_frame + buffer->offset,
		vulkan_utils_format_to_stride(image->info.format) * image->width * image->height);

	return true;
}

bool vulkan_image_prepare_attachment(VulkanContext *context, RhiImage image_handle) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, image_handle, MAX_IMAGES, true, false);

	VkImageLayout new_layout = image->aspect == VK_IMAGE_ASPECT_COLOR_BIT
		? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		: VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	_vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], new_layout);

	return true;
}
bool vulkan_image_prepare_sample(VulkanContext *context, RhiImage image_handle) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, image_handle, MAX_IMAGES, true, false);

	VkImageLayout new_layout = image->aspect == VK_IMAGE_ASPECT_COLOR_BIT
		? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		: VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

	_vulkan_image_transition_auto(image, context->command_buffers[context->current_frame], new_layout);

	return true;
}

bool vulkan_image_resize(VulkanContext *context, RhiImage image_handle, uint32_t width,
	uint32_t height) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, image_handle, MAX_IMAGES, true, false);

	vkDestroyImageView(context->device.logical, image->view, NULL);
	vkDestroyImage(context->device.logical, image->handle, NULL);
	vkFreeMemory(context->device.logical, image->memory, NULL);

	_vulkan_image_make(context, image->info.samples, width, height, image->info.format,
		VK_IMAGE_TILING_OPTIMAL, image->info.usage, image->type,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image);
	_vulkan_imageview_make(context, to_view_type(image->type), image->aspect, image);

	return true;
}

uint32x2 vulkan_image_size(VulkanContext *context, RhiImage image_handle) {
	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, image_handle, MAX_IMAGES, true, (uint32x2){ 0 });

	uint32x2 result = {
		.x = image->info.extent.width,
		.y = image->info.extent.height,
	};
	return result;
}

RhiSampler vulkan_sampler_make(VulkanContext *context, SamplerDesc description) {
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
		.borderColor = (VkBorderColor)description.border_color,
		.unnormalizedCoordinates = VK_FALSE
	};

	if (vkCreateSampler(context->device.logical, &sampler->info, NULL, &sampler->handle) !=
		VK_SUCCESS) {
		LOG_ERROR("Failed to create VkSampler");
		return INVALID_RHI(RhiSampler);
	}

	LOG_INFO("sampler[ID = %u] loaded successfully (%-7s | %s)",
		indexof(context->sampler_pool, sampler),
		description.min_filter == FILTER_NEAREST ? "NEAREST" : "LINEAR",
		description.mag_filter == FILTER_NEAREST ? "NEAREST" : "LINEAR");
	sampler->state = VULKAN_RESOURCE_STATE_INITIALIZED;

	return (RhiSampler){ indexof(context->sampler_pool, sampler) };
}

bool vulkan_sampler_destroy(VulkanContext *context, RhiSampler handle) {
	VulkanSampler *sampler = NULL;
	VULKAN_GET_OR_RETURN(sampler, context->sampler_pool, handle, MAX_SAMPLERS, true, false);

	vkDestroySampler(context->device.logical, sampler->handle, NULL);
	*sampler = (VulkanSampler){ 0 };

	pool_free(context->sampler_pool, sampler);

	return true;
}

uint32_t to_stride(ImageFormat format) {
	switch (format) {
		case IMAGE_FORMAT_RGB8:
		case IMAGE_FORMAT_RGB8_SRGB:
			return 3;

		case IMAGE_FORMAT_RGBA8:
		case IMAGE_FORMAT_RGBA8_SRGB:
			return 4;
		case IMAGE_FORMAT_RGBA16F:
			return 8;
		case IMAGE_FORMAT_RGBA32F:
			return 16;
		case IMAGE_FORMAT_R8:
			return 1;
		case IMAGE_FORMAT_R32:
			return 4;
		case IMAGE_FORMAT_DEPTH:
			return 4;
		case IMAGE_FORMAT_DEPTH_STENCIL:
		case IMAGE_FORMAT_MAX:
			ASSERT_MESSAGE(false, "Not yet implemented");
			return 0;
	}

	return 0;
}

VkImageAspectFlags to_aspect(ImageFormat format) {
	switch (format) {
		case IMAGE_FORMAT_DEPTH:
			return VK_IMAGE_ASPECT_DEPTH_BIT;
		case IMAGE_FORMAT_DEPTH_STENCIL:
			return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		default:
			return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

VkImageViewType to_view_type(ImageType type) {
	switch (type) {
		case IMAGE_TYPE_1D:
			return VK_IMAGE_VIEW_TYPE_1D;
		case IMAGE_TYPE_2D:
			return VK_IMAGE_VIEW_TYPE_2D;
		case IMAGE_TYPE_3D:
			return VK_IMAGE_VIEW_TYPE_3D;
		case IMAGE_TYPE_CUBE:
			return VK_IMAGE_VIEW_TYPE_CUBE;
		default:
			ASSERT(false);
			return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	}
}

VkImageUsageFlags to_usage_flags(ImageFormat format, ImageUsageFlags usage, bool has_pixels) {
	VkImageUsageFlags vk_usage = 0;

	if (has_pixels)
		vk_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	if (FLAG_GET(usage, IMAGE_USAGE_SAMPLED))
		vk_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

	if (FLAG_GET(usage, IMAGE_USAGE_RENDER_TARGET)) {
		if (format == IMAGE_FORMAT_DEPTH || format == IMAGE_FORMAT_DEPTH_STENCIL)
			vk_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		else
			vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	if (FLAG_GET(usage, IMAGE_USAGE_READBACK))
		vk_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	if (vk_usage == 0) {
		LOG_ERROR("Vulkan: image created with neither SAMPLED nor RENDER_TARGET usage");
		ASSERT(false);
		return 0;
	}

	return vk_usage;
}
