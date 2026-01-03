#include "vk_internal.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "common.h"
#include "core/debug.h"
#include "core/logger.h"

bool vulkan_renderer_resource_global_create(VulkanContext *context, uint32_t store_index, size_t size) {
	if (store_index >= MAX_GLOBAL_RESOURCES) {
		LOG_ERROR("Vulkan: global resource index %d out of bounds, aborting vulkan_renderer_resource_global_create", store_index);
		return false;
	}

	VulkanGlobalResource *global = &context->global_resources[store_index];
	if (global->set != NULL) {
		LOG_ERROR("Vulkan: global resource at index %d is already in use, aborting vulkan_renderer_resource_global_create", store_index);
		ASSERT(false);
		return false;
	}
	VulkanBuffer *buffer = &global->buffer;

	global->binding = (VkDescriptorSetLayoutBinding){
		.binding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
		.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS
	};

	VkDescriptorSetLayoutCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &global->binding,
	};

	if (vkCreateDescriptorSetLayout(context->device.logical, &create_info, NULL, &global->set_layout) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: Failed to create global VkDescriptorSetLayout");
		return false;
	}

	VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &global->set_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &context->global_range,
	};

	if (vkCreatePipelineLayout(context->device.logical, &pipeline_layout_info, NULL, &global->pipeline_layout) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: failed to create VkPipelineLayout for global resource, aborting vulkan_renderer_global_resource_create");
		return false;
	}

	VkDescriptorSetAllocateInfo ds_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = context->descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &global->set_layout
	};

	if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, &global->set) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: failed to create VkDescriptorSet for global resource, aborting vulkan_renderer_global_resource_create");
		return false;
	}

	if (vulkan_buffer_ubo_create(context, buffer, size, NULL) == false) {
		LOG_ERROR("Vulkan: failed to create VkBuffer for global resource, aborting vulkan_renderer_global_resource_create");
		return false;
	}

	VkDescriptorBufferInfo buffer_info = {
		.buffer = buffer->handle,
		.offset = 0,
		.range = buffer->stride,
	};

	VkWriteDescriptorSet descriptor_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = global->set,
		.dstBinding = 0, // TODO: Maybe pass this
		.dstArrayElement = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
		.descriptorCount = 1,
		.pBufferInfo = &buffer_info,
	};
	vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);

	LOG_INFO("Vulkan: Global resource created");
	return true;
}

bool vulkan_renderer_resource_global_write(VulkanContext *context, uint32_t retrieve_index, size_t offset, size_t size, void *data) {
	if (retrieve_index >= MAX_GLOBAL_RESOURCES) {
		LOG_ERROR("Vulkan: global resource index %d out of bounds, aborting vulkan_renderer_resource_global_write", retrieve_index);
		return false;
	}

	VulkanGlobalResource *global = &context->global_resources[retrieve_index];
	if (global->set == NULL) {
		LOG_ERROR("Vulkan: global resource at index %d is not in use, aborting vulkan_renderer_resource_global_write", retrieve_index);
		ASSERT(false);
		return false;
	}

	VulkanBuffer *buffer = &global->buffer;

	vulkan_buffer_write_indexed(buffer, context->current_frame, offset, size, data);
	return true;
}
bool vulkan_renderer_resource_global_bind(VulkanContext *context, uint32_t retrieve_index) {
	if (retrieve_index >= MAX_GLOBAL_RESOURCES) {
		LOG_ERROR("Vulkan: global resource index %d out of bounds, aborting vulkan_renderer_global_resource_bind", retrieve_index);
		return false;
	}

	VulkanGlobalResource *global = &context->global_resources[retrieve_index];
	if (global->set == NULL) {
		LOG_ERROR("Vulkan: global resource at index %d is not in use, aborting vulkan_renderer_global_resource_bind", retrieve_index);
		ASSERT(false);
		return false;
	}

	VulkanBuffer *buffer = &global->buffer;

	uint32_t pass_offsets[] = { buffer->stride * context->current_frame };
	vkCmdBindDescriptorSets(
		context->command_buffers[context->current_frame],
		VK_PIPELINE_BIND_POINT_GRAPHICS, global->pipeline_layout,
		SHADER_UNIFORM_FREQUENCY_PER_FRAME, 1, &global->set, 1, pass_offsets);

	return true;
}

bool vulkan_renderer_resource_group_create(VulkanContext *context, uint32_t store_index, uint32_t shader_index, uint32_t max_instance_count) {
	if (store_index >= MAX_GROUP_RESOURCES || shader_index >= MAX_SHADERS) {
		LOG_ERROR("Vulkan: group or shader index out of bounds, aborting vulkan_renderer_resource_group_create", store_index, shader_index);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[shader_index];
	if (shader->vertex_shader == NULL || shader->fragment_shader == NULL) {
		LOG_ERROR("Vulkan: shader at index %d not in use, aborting vulkan_renderer_resource_group_create");
		ASSERT(false);
		return false;
	}

	VulkanGroupResource *group = &context->group_resources[store_index];
	if (group->set != NULL) {
		LOG_ERROR("Vulkan: group resource at index %d is already in use, aborting vulkan_renderer_group_create", store_index);
		ASSERT(false);
		return false;
	}
	VulkanBuffer *buffer = &group->buffer;
	group->shader_index = shader_index;
	group->max_instance_count = max_instance_count;

	VkDescriptorSetAllocateInfo ds_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = context->descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &shader->group_layout
	};

	if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, &group->set) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan DescriptorSets");
		return false;
	}

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if (vulkan_buffer_create(context, shader->instance_size, max_instance_count * MAX_FRAMES_IN_FLIGHT, usage, memory_properties, buffer) == false) {
		LOG_ERROR("Vulkan: failed to create VkBuffer for group resource, aborting vulkan_renderer_group_create");
		return false;
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

	LOG_INFO("Vulkan: VkDescriptorSet created");

	return true;
}

bool vulkan_renderer_resource_group_write(VulkanContext *context, uint32_t retrieve_index, uint32_t instance_index, size_t offset, size_t size, void *data, bool all_frames) {
	if (retrieve_index >= MAX_GROUP_RESOURCES) {
		LOG_ERROR("Vulkan: group resource index %d out of bounds, aborting vulkan_renderer_resource_group_write", retrieve_index);
		return false;
	}

	VulkanGroupResource *group = &context->group_resources[retrieve_index];
	if (group->set == NULL) {
		LOG_ERROR("Vulkan: group resource at index %d is not in use, aborting vulkan_renderer_resource_group_write", retrieve_index);
		ASSERT(false);
		return false;
	}
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

bool vulkan_renderer_resource_group_bind(VulkanContext *context, uint32_t retrieve_index, uint32_t instance_index) {
	if (retrieve_index >= MAX_GROUP_RESOURCES) {
		LOG_ERROR("Vulkan: group resource index %d out of bounds, aborting vulkan_renderer_resource_group_bind", retrieve_index);
		return false;
	}

	VulkanGroupResource *group = &context->group_resources[retrieve_index];
	if (group->set == NULL) {
		LOG_ERROR("Vulkan: group resource at index %d is not in use, aborting vulkan_renderer_resource_group_bind", retrieve_index);
		ASSERT(false);
		return false;
	}
	VulkanBuffer *buffer = &group->buffer;

	VulkanShader *shader = &context->shader_pool[group->shader_index];
	if (shader->vertex_shader == NULL || shader->fragment_shader == NULL) {
		LOG_ERROR("Vulkan: shader at index %d not in use, aborting vulkan_renderer_resource_group_bind");
		ASSERT(false);
		return false;
	}

	uint32_t frame_offset = context->current_frame * group->max_instance_count;
	uint32_t pass_offsets[] = { buffer->stride * (frame_offset + instance_index) };
	vkCmdBindDescriptorSets(
		context->command_buffers[context->current_frame],
		VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline_layout,
		SHADER_UNIFORM_FREQUENCY_PER_MATERIAL, 1, &group->set, 1, pass_offsets);

	return true;
}

bool vulkan_renderer_resource_group_set_texture_sampler(VulkanContext *context, uint32_t retrieve_index, uint32_t binding, uint32_t texture_index, uint32_t sampler_index) {
	if (retrieve_index >= MAX_GROUP_RESOURCES || texture_index >= MAX_TEXTURES || sampler_index >= MAX_SAMPLERS) {
		LOG_ERROR("Vulkan: group(%d), texture(%d) or sampler(%d) index out of bounds, aborting vulkan_renderer_group_set_texture_sampler", retrieve_index, texture_index, sampler_index);
		return false;
	}

	VulkanGroupResource *group = &context->group_resources[retrieve_index];
	if (group->set == NULL) {
		LOG_ERROR("Vulkan: group resource at index %d is not in use, aborting vulkan_renderer_group_set_texture_sampler", retrieve_index);
		ASSERT(false);
		return false;
	}
	VulkanBuffer *buffer = &group->buffer;

	VulkanImage *image = &context->image_pool[texture_index];
	if (image->handle == NULL) {
		LOG_ERROR("Vulkan: texture at index %d not in use, aborting vulkan_renderer_group_set_texture_sampler");
		ASSERT(false);
		return false;
	}

	VulkanSampler *sampler = &context->sampler_pool[sampler_index];
	if (sampler->handle == NULL) {
		LOG_ERROR("Vulkan: sampler at index %d not in use, aborting vulkan_renderer_group_set_texture_sampler");
		ASSERT(false);
		return false;
	}

	VkDescriptorImageInfo image_info = {
		.sampler = sampler->handle,
		.imageView = image->view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
