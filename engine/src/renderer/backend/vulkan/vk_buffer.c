#include "common.h"
#include "core/arena.h"
#include "core/pool.h"
#include "core/r_types.h"
#include "renderer/r_internal.h"
#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "core/debug.h"
#include "core/logger.h"
#include <string.h>
#include <vulkan/vulkan_core.h>

static VkBufferUsageFlags to_vulkan_usage(BufferUsageFlags type);
static const char *buffer_type_stringify[] = {
	"Readback",
	"Vertex",
	"Index",
	"Uniform",
	"Storage",
	"Transfer",
};

RhiBuffer vulkan_buffer_make(VulkanContext *context, BufferUsageFlags type, BufferMemory memory, size_t size, void *data) {
	VulkanBuffer *buffer = pool_alloc_struct(context->buffer_pool, VulkanBuffer);
	buffer->type = type;

	LOG_TRACE("Vulkan: creating %s buffer...", buffer_type_stringify[type]);

	logger_indent();

	VkBufferUsageFlags usage = to_vulkan_usage(type);
	bool result = false;
	if (memory == BUFFER_MEMORY_SHARED) {
		VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		result = vulkan_buffer_make_internal(context, size, usage, properties, 0, buffer);
		vulkan_buffer_map(context, buffer);
		if (data) {
			for (uint32_t index = 0; index < MAX_FRAMES_IN_FLIGHT; ++index)
				memory_copy((uint8_t *)buffer->mapped + (index * buffer->frame_size), data, size);
		}
	} else {
		usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		result = vulkan_buffer_make_internal(context, size, usage, properties, 0, buffer);

		if (data && vulkan_buffer_upload(context, buffer, 0, size, data) == false)
			return INVALID_RHI(RhiBuffer);
	}

	if (result == false)
		return INVALID_RHI(RhiBuffer);

	LOG_TRACE("Vulkan: %s buffer created", buffer_type_stringify[type]);

	logger_dedent();

	return (RhiBuffer){ indexof(context->buffer_pool, buffer) };
}

bool vulkan_buffer_destroy(VulkanContext *context, RhiBuffer buffer_handle) {
	VulkanBuffer *buffer = NULL;
	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, buffer_handle, MAX_BUFFERS, true, false);

	vkDestroyBuffer(context->device.logical, buffer->handle, NULL);
	vkFreeMemory(context->device.logical, buffer->memory, NULL);
	if (buffer->view)
		vkDestroyBufferView(context->device.logical, buffer->view, NULL);

	*buffer = (VulkanBuffer){ 0 };

	pool_free(context->buffer_pool, buffer);

	return true;
}

bool vulkan_buffer_write_internal(VulkanContext *context, uint32_t frame_index, size_t offset, size_t size, void *data, VulkanBuffer *buffer) {
	if (FLAG_GET(buffer->memory_property_flags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
		if (offset + size > buffer->size) {
			LOG_WARN("Vulkan: write overflows frame allocation by %zuB, clamping", (offset + size) - buffer->frame_size);
			size = buffer->frame_size - offset;
		}

		uint8_t *dest = (uint8_t *)buffer->mapped + (buffer->frame_size * frame_index) + offset;
		memory_copy(dest, data, size);

		return true;
	}

	return vulkan_buffer_upload(context, buffer, offset, size, data);
}

size_t vulkan_buffer_push(VulkanContext *context, RhiBuffer buffer_handle, size_t size) {
	VulkanBuffer *buffer = NULL;
	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, buffer_handle, MAX_BUFFERS, true, 0);

	if (FLAG_GET(buffer->memory_property_flags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
		ASSERT(buffer->offset + size < buffer->frame_size);
	} else {
		ASSERT(buffer->offset + size < buffer->size);
	}

	size_t offset = buffer->offset;
	size_t required_alignment =
		vulkan_memory_required_alignment(context, buffer->usage, buffer->memory_property_flags);
	size_t padded_size = alignup(size, required_alignment);
	buffer->offset += padded_size;

	return offset;
}

bool vulkan_buffer_reset(VulkanContext *context, RhiBuffer buffer_handle) {
	VulkanBuffer *buffer = NULL;
	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, buffer_handle, MAX_BUFFERS, true, false);

	buffer->offset = 0;
	return true;
}

bool vulkan_buffer_write(VulkanContext *context, RhiBuffer buffer_handle, size_t offset, size_t size, void *data) {
	VulkanBuffer *buffer = NULL;
	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, buffer_handle, MAX_BUFFERS, true, false);

	return vulkan_buffer_write_internal(context, context->current_frame, offset, size, data, buffer);
}

bool vulkan_buffer_write_all(VulkanContext *context, RhiBuffer buffer_handle, size_t offset, size_t size, void *data) {
	VulkanBuffer *buffer = NULL;
	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, buffer_handle, MAX_BUFFERS, true, false);

	bool result = true;
	for (uint32_t frame_index = 0; frame_index < MAX_FRAMES_IN_FLIGHT; ++frame_index) {
		if (vulkan_buffer_write_internal(context, frame_index, offset, size, data, buffer) == false)
			result = false;
	}

	return result;
}

bool vulkan_buffer_bind_index(VulkanContext *context, RhiBuffer buffer_handle, size_t offset) {
	const VulkanBuffer *buffer = NULL;
	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, buffer_handle, MAX_BUFFERS, true, false);

	ASSERT(FLAG_GET(buffer->memory_property_flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
	ASSERT(FLAG_GET(buffer->usage, VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
	vkCmdBindIndexBuffer(context->command_buffers[context->current_frame], buffer->handle, offset, VK_INDEX_TYPE_UINT32);
	return true;
}
ENGINE_API bool vulkan_buffer_bind_vertex(VulkanContext *context, RhiBuffer buffer_handle, size_t offset) {
	const VulkanBuffer *buffer = NULL;
	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, buffer_handle, MAX_BUFFERS, true, false);

	ASSERT(FLAG_GET(buffer->memory_property_flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
	ASSERT(FLAG_GET(buffer->usage, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
	vkCmdBindVertexBuffers(context->command_buffers[context->current_frame], 0, 1, &buffer->handle, &offset);
	return true;
}

/* bool vulkan_buffers_bind(VulkanContext *context, RhiBuffer *buffer_handles, uint32_t count) { */
/* 	const VulkanBuffer *buffer = NULL; */
/* 	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, buffer_handles[0], MAX_BUFFERS, true, false); */

/* 	if (buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) { */
/* 		VkBuffer vertex_buffers[16] = { 0 }; */
/* 		VkDeviceSize offsets[16] = { 0 }; */
/* 		for (uint32_t index = 0; index < count; ++index) { */
/* 			vertex_buffers[index] = context->buffer_pool[buffer_handles[index].id].handle; */
/* 			offsets[index] = 0; */
/* 		} */

/* 		vkCmdBindVertexBuffers(context->command_buffers[context->current_frame], 0, count, vertex_buffers, offsets); */
/* 		return true; */
/* 	} */

/* 	if (buffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) { */
/* 		vkCmdBindIndexBuffer(context->command_buffers[context->current_frame], buffer->handle, 0, VK_INDEX_TYPE_UINT32); */
/* 		return true; */
/* 	} */

/* 	LOG_WARN("Buffer failed to bind: Unsupported buffer type"); */

/* 	return false; */
/* } */

bool vulkan_buffer_make_internal(
	VulkanContext *context, VkDeviceSize size,
	VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkFormat format,
	VulkanBuffer *out_buffer) {
	out_buffer->frame_size = size;
	out_buffer->frame_size = alignup(out_buffer->frame_size, vulkan_memory_required_alignment(context, usage, properties));
	out_buffer->size =
		FLAG_GET(properties, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
			? out_buffer->frame_size
			: out_buffer->frame_size * MAX_FRAMES_IN_FLIGHT;

	out_buffer->usage = usage;
	out_buffer->memory_property_flags = properties;

	uint32_t family_indices[] = { context->device.graphics_index };

	VkBufferCreateInfo vb_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = out_buffer->size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = family_indices
	};

	if (vkCreateBuffer(context->device.logical, &vb_create_info, NULL, &out_buffer->handle) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: failed to create buffer");
		return false;
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(context->device.logical, out_buffer->handle, &memory_requirements);

	uint32_t memory_type_index = vulkan_memory_type_find(context->device.physical, memory_requirements.memoryTypeBits, properties);
	size_t allocation_size = memory_requirements.size;

	LOG_TRACE("SIZE = %llu, REQUIREMENT = %llu", size, memory_requirements.size);

	VkMemoryAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = memory_type_index,
	};

	if (vkAllocateMemory(context->device.logical, &allocate_info, NULL, &out_buffer->memory) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: failed to allocate buffer memory");
		return false;
	}

	if (vkBindBufferMemory(context->device.logical, out_buffer->handle, out_buffer->memory, 0) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: failed to bind buffer memory");
		return false;
	}

	if (format) {
		VkFormatProperties format_properties = { 0 };
		vkGetPhysicalDeviceFormatProperties(context->device.physical, format, &format_properties);

		if (FLAG_GET(usage, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) &&
			FLAG_GET(format_properties.bufferFeatures, VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT) == false) {
			ASSERT(false);
		} else if (FLAG_GET(usage, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) &&
				   FLAG_GET(format_properties.bufferFeatures, VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT) == false) {
			ASSERT(false);
		}

		VkBufferViewCreateInfo view_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
			.buffer = out_buffer->handle,
			.format = format,
			.offset = 0,
			.range = size
		};

		if (vkCreateBufferView(context->device.logical, &view_info, NULL, &out_buffer->view) != VK_SUCCESS) {
			LOG_ERROR("Vulkan: failed to create buffer view");
			return false;
		}
	}

	LOG_TRACE("VkBuffer created");
	out_buffer->state = VULKAN_RESOURCE_STATE_INITIALIZED;
	return true;
}

bool vulkan_buffer_map(VulkanContext *context, VulkanBuffer *buffer) {
	return vkMapMemory(context->device.logical, buffer->memory, 0, buffer->frame_size * MAX_FRAMES_IN_FLIGHT, 0, &buffer->mapped) == VK_SUCCESS;
}

void vulkan_buffer_unmap(VulkanContext *context, VulkanBuffer *buffer) {
	vkUnmapMemory(context->device.logical, buffer->memory);
}

bool vulkan_buffer_upload(VulkanContext *context, VulkanBuffer *dst, size_t offset, size_t size, void *data) {
	VulkanBuffer *staging_buffer = &context->staging_buffer;
	size_t copy_end = alignup(staging_buffer->offset + size, context->device.properties.limits.minMemoryMapAlignment);
	size_t copy_start = staging_buffer->frame_size * context->current_frame + staging_buffer->offset;
	if (copy_end >= staging_buffer->frame_size) {
		ASSERT_MESSAGE(false, "Max staging buffer size exceeded, aborting vulkan_buffer_create");
		return false;
	}

	memory_copy((uint8_t *)staging_buffer->mapped + copy_start, data, size);
	vulkan_buffer_to_buffer(context, copy_start, staging_buffer->handle, offset, dst->handle, size);
	staging_buffer->offset = copy_end;

	return true;
}

bool vulkan_buffer_to_buffer(VulkanContext *context, VkDeviceSize src_offset, VkBuffer src, VkDeviceSize dst_offset, VkBuffer dst, VkDeviceSize size) {
	VkCommandBuffer command_buffer;
	vulkan_command_oneshot_begin(context, context->graphics_command_pool, &command_buffer);

	LOG_TRACE("Copying region of %llu from %p to %p", size, src, dst);
	VkBufferCopy copy_region = { .srcOffset = src_offset, .dstOffset = dst_offset, .size = size };
	vkCmdCopyBuffer(command_buffer, src, dst, 1, &copy_region);

	VkBufferMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = dst,
		.offset = 0,
		.size = size
	};

	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, NULL, 1, &barrier, 0, NULL);
	vulkan_command_oneshot_end(context, context->device.graphics_queue, context->graphics_command_pool, &command_buffer);
	return true;
}

bool vulkan_buffer_to_image(VulkanContext *context, VkDeviceSize src_offset, VkBuffer src, VkImage dst, uint32_t width, uint32_t height, uint32_t layer_count, VkDeviceSize layer_size) {
	VkCommandBuffer command_buffer;
	vulkan_command_oneshot_begin(context, context->graphics_command_pool, &command_buffer);
	vulkan_image_transition(
		context, command_buffer, dst, VK_IMAGE_ASPECT_COLOR_BIT, layer_count,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, VK_ACCESS_TRANSFER_WRITE_BIT);

	ArenaTemp scratch = arena_scratch_begin(NULL);
	VkBufferImageCopy *regions = arena_push_count(scratch.arena, layer_count, VkBufferImageCopy);

	for (uint32_t layer_index = 0; layer_index < layer_count; ++layer_index) {
		regions[layer_index] = (VkBufferImageCopy){
			.bufferOffset = src_offset + layer_index * layer_size,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource = {
			  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			  .mipLevel = 0,
			  .baseArrayLayer = layer_index,
			  .layerCount = 1,
			},
			.imageOffset = { 0 },
			.imageExtent = { .width = width, .height = height, .depth = 1 },
		};
	}

	vkCmdCopyBufferToImage(command_buffer, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, layer_count, regions);

	vulkan_image_transition(
		context, command_buffer, dst, VK_IMAGE_ASPECT_COLOR_BIT, layer_count,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

	vulkan_command_oneshot_end(context, context->device.graphics_queue, context->graphics_command_pool, &command_buffer);
	arena_scratch_end(scratch);

	return true;
}

VkBufferUsageFlags to_vulkan_usage(BufferUsageFlags usage) {
	VkBufferUsageFlags result = 0;

	if (FLAG_GET(usage, BUFFER_USAGE_VERTEX)) {
		result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}
	if (FLAG_GET(usage, BUFFER_USAGE_INDEX)) {
		result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}
	if (FLAG_GET(usage, BUFFER_USAGE_UNIFORM)) {
		result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	}
	if (FLAG_GET(usage, BUFFER_USAGE_STORAGE)) {
		result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}
	if (FLAG_GET(usage, BUFFER_USAGE_TRANSFER)) {
		result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	}

	ASSERT(result);
	return result;
}
