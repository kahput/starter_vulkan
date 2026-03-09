#include "core/pool.h"
#include "core/r_types.h"
#include "vk_internal.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "core/debug.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

VkDescriptorType to_vulkan_descriptor_type(ShaderBindingType type);

RhiUniformSet vulkan_uniformset_push(
	VulkanContext *context, RhiShader rshader, uint32_t set_number) {
	VulkanShader *shader = NULL;
	VULKAN_GET_OR_RETURN(shader, context->shader_pool, rshader, MAX_UNIFORM_SETS, true, INVALID_RHI(RhiUniformSet));

	VulkanUniformSet *set = pool_alloc_struct(context->set_pool, VulkanUniformSet);
	set->number = set_number;
	if (indexof(context->set_pool, set) == 0) {
		LOG_INFO("The set is 0 for some reason");
	}

	// TODO: Keep reflection around and copy it to uniform set here
	// set->bindings = shader->bindings;

	VkDescriptorSetAllocateInfo ds_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = context->descriptor_pools[context->current_frame],
		.descriptorSetCount = 1,
		.pSetLayouts = &shader->layouts[set_number]
	};

	if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, &set->handle) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan DescriptorSets");
		return INVALID_RHI(RhiUniformSet);
	}

	set->state = VULKAN_RESOURCE_STATE_INITIALIZED;
	return (RhiUniformSet){ indexof(context->set_pool, set) };
}

bool vulkan_uniformset_destroy(VulkanContext *context, RhiUniformSet rset) {
	VulkanUniformSet *set = NULL;
	VULKAN_GET_OR_RETURN(set, context->set_pool, rset, MAX_UNIFORM_SETS, true, false);

	pool_free(context->set_pool, set);

	return true;
}

bool vulkan_uniformset_bind_buffer(
	VulkanContext *context, RhiUniformSet rset,
	uint32_t binding, RhiBuffer rbuffer) {
	VulkanUniformSet *set = NULL;
	VULKAN_GET_OR_RETURN(set, context->set_pool, rset, MAX_UNIFORM_SETS, true, false);

	VulkanBuffer *buffer = NULL;
	VULKAN_GET_OR_RETURN(buffer, context->buffer_pool, rbuffer, MAX_BUFFERS, true, false);

	set->buffer = buffer;

	VkDescriptorBufferInfo buffer_info = {
		.buffer = buffer->handle,
		.offset = 0,
		.range = buffer->stride
	};

	VkWriteDescriptorSet descriptor_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set->handle,
		.dstBinding = binding,
		.dstArrayElement = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, // TODO: Can't assume dynamic
		.descriptorCount = 1,
		.pBufferInfo = &buffer_info
	};
	vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);

	return true;
}
bool vulkan_uniformset_bind_texture(
	VulkanContext *context, RhiUniformSet rset,
	uint32_t binding, RhiTexture rtexture, RhiSampler rsampler) {
	VulkanUniformSet *set = NULL;
	VULKAN_GET_OR_RETURN(set, context->set_pool, rset, MAX_UNIFORM_SETS, true, false);

	VulkanImage *image = NULL;
	VULKAN_GET_OR_RETURN(image, context->image_pool, rtexture, MAX_TEXTURES, true, false);

	VulkanSampler *sampler = NULL;
	VULKAN_GET_OR_RETURN(sampler, context->sampler_pool, rsampler, MAX_SAMPLERS, true, false);

	VkDescriptorImageInfo image_info = {
		.sampler = sampler->handle,
		.imageView = image->view,
		.imageLayout = image->aspect == VK_IMAGE_ASPECT_COLOR_BIT ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
	};

	VkWriteDescriptorSet descriptor_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set->handle,
		.dstBinding = binding,
		.dstArrayElement = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.pImageInfo = &image_info,
	};
	vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);

	return true;
}

bool vulkan_uniformset_bind(VulkanContext *context, RhiUniformSet rset) {
	VulkanUniformSet *set = NULL;
	VULKAN_GET_OR_RETURN(set, context->set_pool, rset, MAX_UNIFORM_SETS, true, false);

	VulkanShader *shader = context->bound_shader;
	ASSERT(shader);

	if (set->buffer && set->buffer->state) {
		VulkanBuffer *buffer = set->buffer;

		uint32_t pass_offsets[] = { buffer->stride * context->current_frame };
		vkCmdBindDescriptorSets(
			context->command_buffers[context->current_frame],
			VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline_layout,
			set->number, 1, &set->handle, 1, pass_offsets);
	} else
		vkCmdBindDescriptorSets(
			context->command_buffers[context->current_frame],
			VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline_layout,
			set->number, 1, &set->handle, 0, NULL);

	return true;
}

bool vulkan_push_constants(VulkanContext *context, size_t offset, size_t size, void *data) {
	VulkanShader *shader = context->bound_shader;
	if (shader == NULL) {
		LOG_ERROR("Vulkan: No shader currently bound, aborting vulkan_renderer_resource_local_write");
		return false;
	}

	vkCmdPushConstants(context->command_buffers[context->current_frame], shader->pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, size, data);
	return true;
}

VkDescriptorType to_vulkan_descriptor_type(ShaderBindingType type) {
	switch (type) {
		case SHADER_BINDING_UNIFORM_BUFFER: {
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		} break;
		case SHADER_BINDING_STORAGE_BUFFER: {
			return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
		} break;
		case SHADER_BINDING_TEXTURE_2D: {
			return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		} break;
		case SHADER_BINDING_SAMPLER: {
			return VK_DESCRIPTOR_TYPE_SAMPLER;
		} break;
		default: {
			ASSERT(false);
			return VK_DESCRIPTOR_TYPE_MAX_ENUM;
		} break;
	}
}
