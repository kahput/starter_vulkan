#include "vk_internal.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "common.h"
#include "core/debug.h"
#include "core/logger.h"

#include <vulkan/vulkan_core.h>

bool vulkan_renderer_global_resource_set_buffer(VulkanContext *context, uint32_t buffer_index) {
	VulkanBuffer *uniform_buffer = &context->buffer_pool[buffer_index];
	if (uniform_buffer->handle[0] == NULL) {
		ASSERT_FORMAT(false, "Vulkan: Buffer target at index %d is not in use", buffer_index);
		return false;
	}

	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
		VkDescriptorBufferInfo buffer_info = {
			.buffer = uniform_buffer->handle[frame],
			.offset = 0,
			.range = VK_WHOLE_SIZE,
		};

		// TODO: Do not hard-code
		VkWriteDescriptorSet descriptor_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = context->global_set.sets[frame],
			.dstBinding = SHADER_UNIFORM_FREQUENCY_PER_FRAME,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo = &buffer_info,
		};

		vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);
	}

	return true;
}

bool vulkan_renderer_push_constants(VulkanContext *context, uint32_t shader_index, size_t offset, size_t size, void *data) {
	VulkanShader *shader = &context->shader_pool[shader_index];
	if (shader->vertex_shader == NULL || shader->fragment_shader == NULL) {
		ASSERT_FORMAT(false, "Vulkan: Frontend renderer tried to push constants to shader at index %d, but index is not in use", shader_index);
		return false;
	}

	vkCmdPushConstants(context->command_buffers[context->current_frame], shader->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, size, data);
	return true;
}

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

bool vulkan_descriptor_global_create(VulkanContext *context) {
	VkDescriptorSetLayoutBinding binding = (VkDescriptorSetLayoutBinding){
		.binding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
	};

	VkDescriptorSetLayoutCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	if (vkCreateDescriptorSetLayout(context->device.logical, &create_info, NULL, &context->globa_set_layout) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: Failed to create global VkDescriptorSetLayout");
		return false;
	}

	VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
		layouts[frame] = context->globa_set_layout;
	}

	VkDescriptorSetAllocateInfo ds_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = context->descriptor_pool,
		.descriptorSetCount = countof(layouts),
		.pSetLayouts = layouts
	};

	if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, context->global_set.sets) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan DescriptorSets");
		return false;
	}

	LOG_INFO("Vulkan: VkDescriptorSet created");

	return true;
}
