#include "vk_internal.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "common.h"
#include "core/debug.h"
#include "core/logger.h"

#include <vulkan/vulkan_core.h>

// bool vulkan_renderer_renderpass_resource_set_buffer(VulkanContext *context, uint32_t buffer_index) {
// 	VulkanBuffer *uniform_buffer = &context->buffer_pool[buffer_index];
// 	if (uniform_buffer->handle == NULL) {
// 		ASSERT_FORMAT(false, "Vulkan: Buffer at index %d is not in use, aborting vulkan_renderer_global_resource_set_buffer", buffer_index);
// 		return false;
// 	}
//
// 	context->main_pass.buffer = uniform_buffer;
//
// 	size_t aligned_size = aligned_address(uniform_buffer->size, uniform_buffer->required_alignment);
// 	VkDescriptorBufferInfo buffer_info = {
// 		.buffer = uniform_buffer->handle,
// 		.offset = 0,
// 		.range = uniform_buffer->stride,
// 	};
//
// 	VkWriteDescriptorSet descriptor_write = {
// 		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
// 		.dstSet = context->main_pass.set,
// 		.dstBinding = SHADER_UNIFORM_FREQUENCY_PER_FRAME,
// 		.dstArrayElement = 0,
// 		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
// 		.descriptorCount = 1,
// 		.pBufferInfo = &buffer_info,
// 	};
//
// 	vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);
//
// 	return true;
// }

bool vulkan_descriptor_layout_create(VulkanContext *context, VkDescriptorSetLayoutBinding *bindings, uint32_t binding_count, VkDescriptorSetLayout *out_layout) {
	VkDescriptorSetLayoutCreateInfo dsl_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = binding_count,
		.pBindings = bindings,
	};

	if (vkCreateDescriptorSetLayout(context->device.logical, &dsl_create_info, NULL, out_layout) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: Failed to create descriptor set layout");
		return false;
	}

	return true;
}

bool vulkan_descriptor_pool_create(VulkanContext *context) {
	VkDescriptorPoolSize sizes[] = {
		{
		  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		  .descriptorCount = 1000,
		},
		{
		  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
		  .descriptorCount = 1000,
		},
		{
		  .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		  .descriptorCount = 1000,
		},
	};

	VkDescriptorPoolCreateInfo dp_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = countof(sizes),
		.pPoolSizes = sizes,
		.maxSets = 1000,
	};

	if (vkCreateDescriptorPool(context->device.logical, &dp_create_info, NULL, &context->descriptor_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan DescriptorPool");
		return false;
	}

	LOG_INFO("VkDescriptorPool created");
	return true;
}

// bool vulkan_descriptor_global_create(VulkanContext *context) {
// 	context->main_pass.binding = (VkDescriptorSetLayoutBinding){
// 		.binding = 0,
// 		.descriptorCount = 1,
// 		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
// 		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
// 	};
//
// 	VkDescriptorSetLayoutCreateInfo create_info = {
// 		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
// 		.bindingCount = 1,
// 		.pBindings = &context->main_pass.binding,
// 	};
//
// 	if (vkCreateDescriptorSetLayout(context->device.logical, &create_info, NULL, &context->main_pass.set_layout) != VK_SUCCESS) {
// 		LOG_ERROR("Vulkan: Failed to create global VkDescriptorSetLayout");
// 		return false;
// 	}
// 	context->main_pass.push_constant_range = (VkPushConstantRange){
// 		.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
// 		.offset = 0,
// 		.size = 128
// 	};
//
// 	VkPipelineLayoutCreateInfo pipeline_layout_info = {
// 		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
// 		.setLayoutCount = 1,
// 		.pSetLayouts = &context->main_pass.set_layout,
// 		.pushConstantRangeCount = 1,
// 		.pPushConstantRanges = &context->main_pass.push_constant_range,
// 	};
//
// 	if (vkCreatePipelineLayout(context->device.logical, &pipeline_layout_info, NULL, &context->main_pass.pipeline_layout) != VK_SUCCESS) {
// 		LOG_ERROR("Failed to create pipeline layout");
// 		return false;
// 	}
//
// 	VkDescriptorSetAllocateInfo ds_allocate_info = {
// 		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
// 		.descriptorPool = context->descriptor_pool,
// 		.descriptorSetCount = 1,
// 		.pSetLayouts = &context->main_pass.set_layout
// 	};
//
// 	if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, &context->main_pass.set) != VK_SUCCESS) {
// 		LOG_ERROR("Failed to create Vulkan DescriptorSets");
// 		return false;
// 	}
//
// 	LOG_INFO("Vulkan: VkDescriptorSet created");
//
// 	return true;
// }
