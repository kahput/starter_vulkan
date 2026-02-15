#include "common.h"
#include "core/arena.h"
#include "core/pool.h"
#include "core/r_types.h"
#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "core/debug.h"
#include "core/logger.h"
#include <string.h>
#include <vulkan/vulkan_core.h>

int32_t to_vulkan_usage(BufferType type);

RhiBuffer vulkan_renderer_buffer_create(VulkanContext *context, BufferType type, size_t size, void *data) {
	VulkanBuffer *buffer = pool_alloc_struct(context->buffer_pool, VulkanBuffer);

	const char *stringify[] = {
		"Vertex buffer",
		"Index buffer",
		"Uniform buffer",
	};

	LOG_TRACE("Vulkan: Creating %s resource...", stringify[type]);

	logger_indent();

	if (type == BUFFER_TYPE_UNIFORM) {
		buffer->count = MAX_FRAMES_IN_FLIGHT;
		bool result = vulkan_buffer_ubo_create(context, buffer, size, data);
		if (result == false)
			return INVALID_RHI(RhiBuffer);
		data = NULL;
	} else {
		bool result = vulkan_buffer_create(context, size, 1, VK_BUFFER_USAGE_TRANSFER_DST_BIT | to_vulkan_usage(type), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer);
	}

	if (data != NULL) {
		VulkanBuffer *staging_buffer = &context->staging_buffer;
		size_t copy_end = aligned_address(staging_buffer->offset + buffer->size, context->device.properties.limits.minMemoryMapAlignment);
		size_t copy_start = staging_buffer->stride * context->current_frame + staging_buffer->offset;
		if (copy_end >= staging_buffer->size) {
			LOG_TRACE("Max staging buffer size exceeded, aborting vulkan_buffer_create");
			ASSERT(false);
			return INVALID_RHI(RhiBuffer);
		}

		vulkan_buffer_write_indexed(staging_buffer, context->current_frame, staging_buffer->offset, size, data);

		vulkan_buffer_to_buffer(context, copy_start, staging_buffer->handle, 0, buffer->handle, buffer->size);
		staging_buffer->offset = copy_end;
	}
	LOG_TRACE("%s resource created", stringify[type]);

	logger_dedent();

	return (RhiBuffer){ pool_index_of(context->buffer_pool, buffer) };
}

bool vulkan_renderer_buffer_destroy(VulkanContext *context, RhiBuffer rbuffer) {
	VulkanBuffer *buffer = NULL;
	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, rbuffer, MAX_BUFFERS, true, false);

	vkDestroyBuffer(context->device.logical, buffer->handle, NULL);
	vkFreeMemory(context->device.logical, buffer->memory, NULL);
	*buffer = (VulkanBuffer){ 0 };

	pool_free(context->buffer_pool, buffer);

	return true;
}

bool vulkan_renderer_buffer_write(VulkanContext *context, RhiBuffer rbuffer, size_t offset, size_t size, void *data) {
	VulkanBuffer *buffer = NULL;
	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, rbuffer, MAX_BUFFERS, true, false);

	if (FLAG_GET(buffer->memory_property_flags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
		size_t copy_size = size;
		if (offset + copy_size > buffer->size) {
			LOG_WARN("Vulkan: write size greater than allocation size, ignoring difference of %dB", size - buffer->size);
			copy_size = buffer->size - offset;
		}

		size_t aligned_size = aligned_address(buffer->size, buffer->required_alignment);
		uint8_t *dest = (uint8_t *)buffer->mapped + (aligned_size * context->current_frame) + offset;
		memcpy(dest, data, copy_size);

		return true;
	}

	ASSERT(buffer->usage != VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

	VulkanBuffer *staging_buffer = &context->staging_buffer;
	size_t copy_end = aligned_address(staging_buffer->offset + buffer->size, context->device.properties.limits.minMemoryMapAlignment);
	size_t copy_start = staging_buffer->stride * context->current_frame + staging_buffer->offset;
	if (copy_end >= staging_buffer->size) {
		LOG_TRACE("Vulkan: staging buffer size exceeded for frame %d, aborting %s", context->current_frame, __func__);
		ASSERT(false);
		return false;
	}

	vulkan_buffer_write_indexed(staging_buffer, context->current_frame, staging_buffer->offset, size, data);

	vulkan_buffer_to_buffer(context, copy_start, staging_buffer->handle, 0, buffer->handle, buffer->size);
	staging_buffer->offset = copy_end;

	return true;
}

bool vulkan_renderer_buffer_bind(VulkanContext *context, RhiBuffer rbuffer, size_t index_size) {
	const VulkanBuffer *buffer = NULL;
	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, rbuffer, MAX_BUFFERS, true, false);

	if (buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
		VkBuffer vertex_buffer[] = { buffer->handle };
		VkDeviceSize offset[] = { 0 };

		vkCmdBindVertexBuffers(context->command_buffers[context->current_frame], 0, 1, vertex_buffer, offset);
		return true;
	}

	if (buffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
		ASSERT(index_size == 4 || index_size == 2);
		vkCmdBindIndexBuffer(context->command_buffers[context->current_frame], buffer->handle, 0, index_size == 4 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
		return true;
	}

	LOG_WARN("Buffer failed to bind: Unsupported buffer type");

	return false;
}

bool vulkan_renderer_buffers_bind(VulkanContext *context, RhiBuffer *rbuffers, uint32_t count) {
	const VulkanBuffer *buffer = NULL;
	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, rbuffers[0], MAX_BUFFERS, true, false);

	if (buffer->usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) {
		VkBuffer vertex_buffers[16] = { 0 };
		VkDeviceSize offsets[16] = { 0 };
		for (uint32_t index = 0; index < count; ++index) {
			vertex_buffers[index] = context->buffer_pool[rbuffers[index].id].handle;
			offsets[index] = 0;
		}

		vkCmdBindVertexBuffers(context->command_buffers[context->current_frame], 0, count, vertex_buffers, offsets);
		return true;
	}

	if (buffer->usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) {
		vkCmdBindIndexBuffer(context->command_buffers[context->current_frame], buffer->handle, 0, VK_INDEX_TYPE_UINT32);
		return true;
	}

	LOG_WARN("Buffer failed to bind: Unsupported buffer type");

	return false;
}

bool vulkan_buffer_ubo_create(VulkanContext *context, VulkanBuffer *buffer, size_t size, void *data) {
	vulkan_buffer_create(
		context, size, MAX_FRAMES_IN_FLIGHT,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		buffer);

	vulkan_buffer_memory_map(context, buffer);

	if (data) {
		for (uint32_t index = 0; index < MAX_FRAMES_IN_FLIGHT; ++index) {
			memcpy((uint8_t *)buffer->mapped + (index * buffer->stride), data, size);
		}
	}

	return true;
}

bool vulkan_buffer_create(
	VulkanContext *context,
	VkDeviceSize size, uint32_t count, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
	VulkanBuffer *out_buffer) {
	out_buffer->size = size;
	out_buffer->count = count;
	out_buffer->usage = usage;
	out_buffer->memory_property_flags = properties;
	out_buffer->required_alignment = vulkan_memory_required_alignment(context, usage, properties);

	VkDeviceSize aligned_size = aligned_address(out_buffer->size, out_buffer->required_alignment);
	out_buffer->stride = aligned_size;

	uint32_t family_indices[] = { context->device.graphics_index };

	VkBufferCreateInfo vb_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = aligned_size * count,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = family_indices
	};

	if (vkCreateBuffer(context->device.logical, &vb_create_info, NULL, &out_buffer->handle) != VK_SUCCESS) {
		LOG_ERROR("Failed to create vertex buffer");
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
		LOG_ERROR("Failed to allocate buffer memory");
		return false;
	}

	if (vkBindBufferMemory(context->device.logical, out_buffer->handle, out_buffer->memory, 0) != VK_SUCCESS) {
		LOG_ERROR("Failed to bind buffer memory");
		return false;
	}

	LOG_TRACE("VkBuffer created");
	out_buffer->state = VULKAN_RESOURCE_STATE_INITIALIZED;
	return true;
}

bool vulkan_buffer_memory_map(VulkanContext *context, VulkanBuffer *buffer) {
	return vkMapMemory(context->device.logical, buffer->memory, 0, buffer->stride * buffer->count, 0, &buffer->mapped) == VK_SUCCESS;
}

void vulkan_buffer_memory_unmap(VulkanContext *context, VulkanBuffer *buffer) {
	vkUnmapMemory(context->device.logical, buffer->memory);
}

void vulkan_buffer_write(VulkanBuffer *buffer, size_t offset, size_t size, void *data) {
	vulkan_buffer_write_indexed(buffer, 0, offset, size, data);
}

void vulkan_buffer_write_indexed(VulkanBuffer *buffer, uint32_t index, size_t offset, size_t size, void *data) {
	if (buffer->handle == NULL || (buffer->memory_property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == false || index > buffer->count || offset + size > buffer->stride) {
		LOG_ERROR("Vulkan: Invalid arguments passed, aborting vulkan_buffer_write");
		ASSERT(false);
	}

	size_t copy_start = (buffer->stride * index) + offset;
	memcpy((uint8_t *)buffer->mapped + copy_start, data, size);
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

	ArenaTemp scratch = arena_scratch(NULL);
	VkBufferImageCopy *regions = arena_push_array_zero(scratch.arena, VkBufferImageCopy, layer_count);

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
	arena_release_scratch(scratch);

	return true;
}

bool vulkan_buffer_allocate(struct vulkan_context *ctx, VkDeviceSize size) {
	return true;
}

int32_t to_vulkan_usage(BufferType type) {
	switch (type) {
		case BUFFER_TYPE_VERTEX:
			return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		case BUFFER_TYPE_INDEX:
			return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		case BUFFER_TYPE_UNIFORM:
			return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		default:
			ASSERT(false);
			return -1;
	}
}
