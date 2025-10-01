#include "core/logger.h"
#include "vk_renderer.h"
#include <vulkan/vulkan_core.h>

bool vk_create_descriptor_set_layout(VKRenderer *renderer) {
	VkDescriptorSetLayoutBinding mvp_layout_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
	};

	VkDescriptorSetLayoutCreateInfo dsl_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &mvp_layout_binding,
	};

	if (vkCreateDescriptorSetLayout(renderer->logical_device, &dsl_create_info, NULL, &renderer->descriptor_set_layout) != VK_SUCCESS) {
		LOG_ERROR("Failed to create descriptor set layout");
		return false;
	}

	return true;
}

bool vk_create_descriptor_pool(VKRenderer *renderer) {
	VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = MAX_FRAMES_IN_FLIGHT
	};

	VkDescriptorPoolCreateInfo dp_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
		.maxSets = MAX_FRAMES_IN_FLIGHT
	};

	if (vkCreateDescriptorPool(renderer->logical_device, &dp_create_info, NULL, &renderer->descriptor_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan DescriptorPool");
		return false;
	}

	LOG_INFO("Vulkan DescriptorPool created");
	return true;
}

bool vk_create_descriptor_set(VKRenderer *renderer) {
	VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		layouts[i] = renderer->descriptor_set_layout;
	}

	VkDescriptorSetAllocateInfo ds_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = renderer->descriptor_pool,
		.descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
		.pSetLayouts = layouts
	};

	if (vkAllocateDescriptorSets(renderer->logical_device, &ds_allocate_info, renderer->descriptor_sets) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan DescriptorSets");
		return false;
	}

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		VkDescriptorBufferInfo buffer_info = {
			.buffer = renderer->uniform_buffers[i],
			.offset = 0,
			.range = sizeof(MVPObject),
		};

		VkWriteDescriptorSet descriptor_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = renderer->descriptor_sets[i],
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo = &buffer_info
		};

		vkUpdateDescriptorSets(renderer->logical_device, 1, &descriptor_write, 0, NULL);
	}

	LOG_INFO("Vulkan DescriptorSets created");
	return true;
}
