#include "core/logger.h"
#include "vk_renderer.h"

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

bool vk_create_texture_image(struct arena *arena, VKRenderer *renderer) {
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

	QueueFamilyIndices queue_familes = find_queue_families(arena, renderer);
	vk_create_buffer(renderer, queue_familes, size, staging_usage, staging_properties, &staging_buffer, &staging_buffer_memory);

	void *data;
	vkMapMemory(renderer->logical_device, staging_buffer_memory, 0, size, 0, &data);
	memcpy(data, pixels, (size_t)size);
	vkUnmapMemory(renderer->logical_device, staging_buffer_memory);

	stbi_image_free(pixels);

	uint32_t family_indices[] = { queue_familes.graphics, queue_familes.transfer };

	VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R32G32B32A32_SFLOAT,
		.extent = {
		  .width = width,
		  .height = height,
		  .depth = 1,
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_CONCURRENT,
		.queueFamilyIndexCount = array_count(family_indices),
		.pQueueFamilyIndices = family_indices,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	if (vkCreateImage(renderer->logical_device, &image_info, NULL, &renderer->texture_image) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan Texture");
		return false;
	}

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements(renderer->logical_device, renderer->texture_image, &memory_requirements);

	VkMemoryAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = find_memory_type(renderer->physical_device, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
	};

	if (vkAllocateMemory(renderer->logical_device, &allocate_info, NULL, &renderer->texture_image_memory) != VK_SUCCESS) {
		LOG_ERROR("Failed to allocate image memory");
		return false;
	}

	vkBindImageMemory(renderer->logical_device, renderer->texture_image, renderer->texture_image_memory, 0);

	vkDestroyBuffer(renderer->logical_device, staging_buffer, NULL);
	vkFreeMemory(renderer->logical_device, staging_buffer_memory, NULL);

	LOG_INFO("Vulkan Texture created");
	return true;
}
