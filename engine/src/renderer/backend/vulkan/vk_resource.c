#include "core/pool.h"
#include "core/r_types.h"
#include "vk_internal.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "core/debug.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

VkDescriptorType to_vulkan_descriptor_type(ShaderBindingType type);

RhiUniformSet vulkan_renderer_uniform_set_create(
	VulkanContext *context, RhiShader rshader, uint32_t set_number) {
	VulkanShader *shader = NULL;
	VULKAN_GET_OR_RETURN(shader, context->shader_pool, rshader, MAX_SHADERS, true, INVALID_RHI(RhiUniformSet));

	VulkanUniformSet *set = pool_alloc_struct(context->set_pool, VulkanUniformSet);
	set->number = set_number;

	// TODO: Keep reflection around and copy it to uniform set here
	// set->bindings = shader->bindings;

	VkDescriptorSetAllocateInfo ds_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = context->descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &shader->layouts[set_number]
	};

	if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, &set->handle) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan DescriptorSets");
		return INVALID_RHI(RhiUniformSet);
	}

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	LOG_INFO("Vulkan: VkDescriptorSet created");
	set->state = VULKAN_RESOURCE_STATE_INITIALIZED;

	return (RhiUniformSet){ pool_index_of(context->set_pool, set) };
}

RhiUniformSet vulkan_renderer_uniform_set_create_ex(
	VulkanContext *context, ResourceBinding *bindings, uint32_t binding_count) {
	VulkanUniformSet *set = pool_alloc_struct(context->set_pool, VulkanUniformSet);
	set->number = 0;

	VkDescriptorSetLayoutBinding vk_bindings[16] = { 0 };

	uint32_t counter = 0;
	for (uint32_t binding_index = 0; binding_index < binding_count; ++binding_index) {
		ResourceBinding *binding = &bindings[binding_index];
		VkDescriptorSetLayoutBinding *vk_binding = &vk_bindings[binding_index];
		*vk_binding = (VkDescriptorSetLayoutBinding){
			.binding = binding->binding,
			.descriptorCount = binding->count ? binding->count : 1,
			.descriptorType = to_vulkan_descriptor_type(binding->type),
			.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS
		};

		counter++;
	}
	ASSERT_FORMAT(counter == binding_count, "says %d, should be %d", counter, binding_count);

	VkDescriptorSetLayoutCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = binding_count,
		.pBindings = vk_bindings,
	};

	if (vkCreateDescriptorSetLayout(context->device.logical, &create_info, NULL, &set->layout) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: Failed to create global VkDescriptorSetLayout");
		return INVALID_RHI(RhiUniformSet);
	}

	VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &set->layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &context->global_range,
	};

	if (vkCreatePipelineLayout(context->device.logical, &pipeline_layout_info, NULL, &set->pipeline_layout) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: failed to create VkPipelineLayout for global resource, aborting %s", __func__);
		return INVALID_RHI(RhiUniformSet);
	}

	VkDescriptorSetAllocateInfo ds_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = context->descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &set->layout
	};

	if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, &set->handle) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: failed to create VkDescriptorSet for global resource, aborting %s", __func__);
		return INVALID_RHI(RhiUniformSet);
	}

	LOG_INFO("Vulkan: Explicit uniform set created");
	set->state = VULKAN_RESOURCE_STATE_INITIALIZED;

	return (RhiUniformSet){ pool_index_of(context->set_pool, set) };
}

bool vulkan_renderer_uniform_set_destroy(VulkanContext *context, RhiUniformSet rset) {
	VulkanUniformSet *set = NULL;
	VULKAN_GET_OR_RETURN(set, context->set_pool, rset, MAX_UNIFORM_SETS, true, false);

	if (set->pipeline_layout)
		vkDestroyPipelineLayout(context->device.logical, set->pipeline_layout, NULL);
	vkDestroyDescriptorSetLayout(context->device.logical, set->layout, NULL);

	pool_free(context->set_pool, set);

	return true;
}

bool vulkan_renderer_uniform_set_bind_buffer(
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
bool vulkan_renderer_uniform_set_bind_texture(
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

bool vulkan_renderer_uniform_set_bind(VulkanContext *context, RhiUniformSet rset) {
	VulkanUniformSet *set = NULL;
	VULKAN_GET_OR_RETURN(set, context->set_pool, rset, MAX_UNIFORM_SETS, true, false);

	if (set->pipeline_layout == NULL && context->bound_shader == NULL) {
		LOG_ERROR("Cannot bind set %d without a bound shader!", set->number);
		return false;
	}

	if (set->pipeline_layout) {
		if (set->buffer && set->buffer->state) {
			VulkanBuffer *buffer = set->buffer;

			uint32_t pass_offsets[] = { buffer->stride * context->current_frame };
			vkCmdBindDescriptorSets(
				context->command_buffers[context->current_frame],
				VK_PIPELINE_BIND_POINT_GRAPHICS, set->pipeline_layout,
				set->number, 1, &set->handle, 1, pass_offsets);
		} else
			vkCmdBindDescriptorSets(
				context->command_buffers[context->current_frame],
				VK_PIPELINE_BIND_POINT_GRAPHICS, set->pipeline_layout,
				set->number, 1, &set->handle, 0, NULL);
	} else {
		VulkanShader *shader = context->bound_shader;

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
	}

	return true;
}
bool vulkan_renderer_uniform_set_bind_offset(VulkanContext *context, RhiUniformSet rset, size_t offset, size_t size) {
	VulkanUniformSet *set = NULL;
	VULKAN_GET_OR_RETURN(set, context->set_pool, rset, MAX_UNIFORM_SETS, true, false);

	if (set->pipeline_layout == NULL && context->bound_shader == NULL) {
		LOG_ERROR("Cannot bind set %d without a bound shader!", set->number);
		return false;
	}

	if (set->pipeline_layout) {
		if (set->buffer && set->buffer->state) {
			VulkanBuffer *buffer = set->buffer;

			uint32_t pass_offsets[] = { buffer->stride * context->current_frame };
			vkCmdBindDescriptorSets(
				context->command_buffers[context->current_frame],
				VK_PIPELINE_BIND_POINT_GRAPHICS, set->pipeline_layout,
				set->number, 1, &set->handle, 1, pass_offsets);
		} else
			vkCmdBindDescriptorSets(
				context->command_buffers[context->current_frame],
				VK_PIPELINE_BIND_POINT_GRAPHICS, set->pipeline_layout,
				set->number, 1, &set->handle, 0, NULL);
	} else {
		VulkanShader *shader = context->bound_shader;

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
	}

	return true;
}

bool vulkan_renderer_push_constants(VulkanContext *context, size_t offset, size_t size, void *data) {
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
