#include "core/pool.h"
#include "core/r_types.h"
#include "vk_internal.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "core/debug.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

VkDescriptorType to_vulkan_descriptor_type(ShaderBindingType type);

RhiGlobalResource vulkan_renderer_resource_global_create(VulkanContext *context, ResourceBinding *bindings, uint32_t binding_count) {
	VulkanGlobalResource *global = pool_alloc_struct(context->global_resources, VulkanGlobalResource);
	VulkanBuffer *buffer = &global->buffer;

	uint32_t buffer_binding = -1;
	for (uint32_t binding_index = 0; binding_index < binding_count; ++binding_index) {
		ResourceBinding *binding = &bindings[binding_index];
		if (binding->type == SHADER_BINDING_UNIFORM_BUFFER) {
			ASSERT(buffer_binding == (uint32_t)-1);
			buffer_binding = binding->binding;
		}

		for (uint32_t descriptor_binding_index = 0; descriptor_binding_index < binding_count; ++descriptor_binding_index) {
			VkDescriptorSetLayoutBinding *vk_binding = &global->bindings[descriptor_binding_index];

			if (vk_binding->descriptorCount == 0) {
				*vk_binding = (VkDescriptorSetLayoutBinding){
					.binding = binding->binding,
					.descriptorCount = binding->count,
					.descriptorType = to_vulkan_descriptor_type(binding->type),
					.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS
				};
				global->binding_count++;
				break;
			}
		}
	}

	VkDescriptorSetLayoutCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = global->binding_count,
		.pBindings = global->bindings,
	};

	if (vkCreateDescriptorSetLayout(context->device.logical, &create_info, NULL, &global->set_layout) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: Failed to create global VkDescriptorSetLayout");
		return INVALID_RHI(RhiGlobalResource);
	}

	VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &global->set_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &context->global_range,
	};

	if (vkCreatePipelineLayout(context->device.logical, &pipeline_layout_info, NULL, &global->pipeline_layout) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: failed to create VkPipelineLayout for global resource, aborting %s", __func__);
		return INVALID_RHI(RhiGlobalResource);
	}

	VkDescriptorSetAllocateInfo ds_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = context->descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &global->set_layout
	};

	if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, &global->set) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: failed to create VkDescriptorSet for global resource, aborting %s", __func__);
		return INVALID_RHI(RhiGlobalResource);
	}

	if (buffer_binding != (uint32_t)-1) {
		if (vulkan_buffer_ubo_create(context, buffer, bindings[buffer_binding].size, NULL) == false) {
			LOG_ERROR("Vulkan: failed to create VkBuffer for global resource, aborting %s", __func__);
			return INVALID_RHI(RhiGlobalResource);
		}

		VkDescriptorBufferInfo buffer_info = {
			.buffer = buffer->handle,
			.offset = 0,
			.range = buffer->stride,
		};

		VkWriteDescriptorSet descriptor_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = global->set,
			.dstBinding = bindings[0].binding,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
			.pBufferInfo = &buffer_info,
		};
		vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);
	}

	LOG_INFO("Vulkan: Global resource created");
	global->state = VULKAN_RESOURCE_STATE_INITIALIZED;

	return (RhiGlobalResource){ pool_index_of(context->global_resources, global), 0 };
}

bool vulkan_renderer_resource_global_destroy(VulkanContext *context, RhiGlobalResource handle) {
	VulkanGlobalResource *global = NULL;
	VULKAN_GET_OR_RETURN(global, context->global_resources, handle, MAX_GLOBAL_RESOURCES, true, false);

	if (global->buffer.state) {
		vkDestroyBuffer(context->device.logical, global->buffer.handle, NULL);
		vkFreeMemory(context->device.logical, global->buffer.memory, NULL);
		global->buffer = (VulkanBuffer){ 0 };
	}

	vkDestroyPipelineLayout(context->device.logical, global->pipeline_layout, NULL);
	vkDestroyDescriptorSetLayout(context->device.logical, global->set_layout, NULL);

	pool_free(context->global_resources, global);

	return true;
}

bool vulkan_renderer_resource_global_set_texture_sampler(VulkanContext *context, RhiGlobalResource handle, uint32_t binding, RhiTexture rtexture, RhiSampler rsampler) {
	VulkanGlobalResource *global = NULL;
	VULKAN_GET_OR_RETURN(global, context->global_resources, handle, MAX_GROUP_RESOURCES, true, false);
	VulkanBuffer *buffer = &global->buffer;

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
		.dstSet = global->set,
		.dstBinding = binding,
		.dstArrayElement = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.pImageInfo = &image_info,
	};
	vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);

	return true;
}

bool vulkan_renderer_resource_global_write(VulkanContext *context, RhiGlobalResource handle, size_t offset, size_t size, void *data) {
	VulkanGlobalResource *global = NULL;
	VULKAN_GET_OR_RETURN(global, context->global_resources, handle, MAX_GLOBAL_RESOURCES, true, false);
	VulkanBuffer *buffer = &global->buffer;

	if (buffer->state == VULKAN_RESOURCE_STATE_UNINITIALIZED) {
		LOG_WARN("Vulkan: global resource at %d does not have a buffer, aborting %s", __func__);
	}

	vulkan_buffer_write_indexed(buffer, context->current_frame, offset, size, data);
	return true;
}
bool vulkan_renderer_resource_global_bind(VulkanContext *context, RhiGlobalResource handle) {
	VulkanGlobalResource *global = NULL;
	VULKAN_GET_OR_RETURN(global, context->global_resources, handle, MAX_GLOBAL_RESOURCES, true, false);
	VulkanBuffer *buffer = &global->buffer;

	if (buffer->state) {
		uint32_t pass_offsets[] = { buffer->stride * context->current_frame };
		vkCmdBindDescriptorSets(
			context->command_buffers[context->current_frame],
			VK_PIPELINE_BIND_POINT_GRAPHICS, global->pipeline_layout,
			SHADER_UNIFORM_FREQUENCY_PER_FRAME, 1, &global->set, 1, pass_offsets);
	} else
		vkCmdBindDescriptorSets(
			context->command_buffers[context->current_frame],
			VK_PIPELINE_BIND_POINT_GRAPHICS, global->pipeline_layout,
			SHADER_UNIFORM_FREQUENCY_PER_FRAME, 1, &global->set, 0, NULL);

	return true;
}

RhiGroupResource vulkan_renderer_resource_group_create(VulkanContext *context, RhiShader rshader, uint32_t max_instance_count) {
	VulkanShader *shader = NULL;
	if (rshader.id >= MAX_SHADERS) {
		LOG_ERROR("Vulkan: shader index %u out of bounds (max %u), aborting %s",
			(uint32_t)(rshader.id), (uint32_t)(MAX_SHADERS), __func__);
		return INVALID_RHI(RhiGroupResource);
	}

	shader = &context->shader_pool[rshader.id];
	if (shader->state != VULKAN_RESOURCE_STATE_INITIALIZED) {
		LOG_ERROR("Vulkan: shader at index %u is not initialized (State: %d), aborting %s",
			(uint32_t)(rshader.id), (shader)->state, __func__);
		ASSERT(false);
		return INVALID_RHI(RhiGroupResource);
	}
	VulkanGroupResource *group = pool_alloc(context->group_resources);
	VulkanBuffer *buffer = &group->buffer;

	group->shader = rshader;
	group->max_instance_count = max_instance_count;

	VkDescriptorSetAllocateInfo ds_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = context->descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &shader->group_layout
	};

	if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, &group->set) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan DescriptorSets");
		return INVALID_RHI(RhiGroupResource);
	}

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if (shader->instance_size != 0) {
		if (vulkan_buffer_create(context, shader->instance_size, max_instance_count * MAX_FRAMES_IN_FLIGHT, usage, memory_properties, buffer) == false) {
			LOG_ERROR("Vulkan: failed to create VkBuffer for group resource, aborting vulkan_renderer_group_create");
			return INVALID_RHI(RhiGroupResource);
		}
		vulkan_buffer_memory_map(context, buffer);

		VkDescriptorBufferInfo buffer_info = {
			.buffer = buffer->handle,
			.offset = 0,
			.range = buffer->stride,
		};

		VkWriteDescriptorSet descriptor_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = group->set,
			.dstBinding = shader->group_ubo_binding,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
			.pBufferInfo = &buffer_info,
		};
		vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);
	}

	LOG_INFO("Vulkan: VkDescriptorSet created");
	group->state = VULKAN_RESOURCE_STATE_INITIALIZED;

	return (RhiGroupResource){ pool_index_of(context->group_resources, group), 0 };
}

bool vulkan_renderer_resource_group_destroy(VulkanContext *context, RhiGroupResource handle) {
	VulkanGroupResource *group = NULL;
	VULKAN_GET_OR_RETURN(group, context->group_resources, handle, MAX_GROUP_RESOURCES, true, false);

	vkDestroyBuffer(context->device.logical, group->buffer.handle, NULL);
	vkFreeMemory(context->device.logical, group->buffer.memory, NULL);
	group->buffer = (VulkanBuffer){ 0 };

	pool_free(context->group_resources, group);

	return true;
}

bool vulkan_renderer_resource_group_write(VulkanContext *context, RhiGroupResource handle, uint32_t instance_index, size_t offset, size_t size, void *data, bool all_frames) {
	VulkanGroupResource *group = NULL;
	VULKAN_GET_OR_RETURN(group, context->group_resources, handle, MAX_GROUP_RESOURCES, true, false);
	VulkanBuffer *buffer = &group->buffer;

	if (all_frames) {
		for (uint32_t frame_index = 0; frame_index < MAX_FRAMES_IN_FLIGHT; ++frame_index) {
			uint32_t frame_offset = frame_index * group->max_instance_count;
			vulkan_buffer_write_indexed(buffer, frame_offset + instance_index, offset, size, data);
		}
	} else {
		uint32_t frame_offset = context->current_frame * group->max_instance_count;
		vulkan_buffer_write_indexed(buffer, frame_offset + instance_index, offset, size, data);
	}
	return true;
}

bool vulkan_renderer_resource_group_bind(VulkanContext *context, RhiGroupResource handle, uint32_t instance_index) {
	VulkanGroupResource *group = NULL;
	VULKAN_GET_OR_RETURN(group, context->group_resources, handle, MAX_GROUP_RESOURCES, true, false);
	VulkanBuffer *buffer = &group->buffer;

	VulkanShader *shader = NULL;
	VULKAN_GET_OR_RETURN(shader, context->shader_pool, group->shader, MAX_SHADERS, true, false);

	if (buffer->state) {
		uint32_t frame_offset = context->current_frame * group->max_instance_count;
		uint32_t pass_offsets[] = { buffer->stride * (frame_offset + instance_index) };

		vkCmdBindDescriptorSets(
			context->command_buffers[context->current_frame],
			VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline_layout,
			SHADER_UNIFORM_FREQUENCY_PER_MATERIAL, 1, &group->set, 1, pass_offsets);
	} else
		vkCmdBindDescriptorSets(
			context->command_buffers[context->current_frame],
			VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline_layout,
			SHADER_UNIFORM_FREQUENCY_PER_MATERIAL, 1, &group->set, 0, NULL);

	return true;
}

bool vulkan_renderer_resource_group_set_texture_sampler(VulkanContext *context, RhiGroupResource handle, uint32_t binding, RhiTexture rtexture, RhiSampler rsampler) {
	VulkanGroupResource *group = NULL;
	VULKAN_GET_OR_RETURN(group, context->group_resources, handle, MAX_GROUP_RESOURCES, true, false);
	VulkanBuffer *buffer = &group->buffer;

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
		.dstSet = group->set,
		.dstBinding = binding,
		.dstArrayElement = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.pImageInfo = &image_info,
	};

	vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);

	return true;
}

bool vulkan_renderer_resource_local_write(VulkanContext *context, size_t offset, size_t size, void *data) {
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
		default:
			return VK_DESCRIPTOR_TYPE_MAX_ENUM;
			ASSERT(false);
			break;
	}
}
