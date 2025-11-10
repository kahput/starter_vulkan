#include "core/logger.h"
#include "renderer/vk_renderer.h"
#include <vulkan/vulkan_core.h>

bool vk_create_descriptor_set_layout(VulkanState *vk_state) {
	VkDescriptorSetLayoutBinding mvp_layout_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
	};

	VkDescriptorSetLayoutBinding sampler_layout_binding = {
		.binding = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
	};

	VkDescriptorSetLayoutBinding bindings[] = { mvp_layout_binding, sampler_layout_binding };

	VkDescriptorSetLayoutCreateInfo dsl_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = array_count(bindings),
		.pBindings = bindings,
	};

	if (vkCreateDescriptorSetLayout(vk_state->device.logical, &dsl_create_info, NULL, &vk_state->descriptor_set_layout) != VK_SUCCESS) {
		LOG_ERROR("Failed to create descriptor set layout");
		return false;
	}

	return true;
}

bool vk_create_descriptor_pool(VulkanState *vk_state) {
	VkDescriptorPoolSize sizes[] = {
		{
		  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		  .descriptorCount = MAX_FRAMES_IN_FLIGHT,
		},
		{
		  .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		  .descriptorCount = MAX_FRAMES_IN_FLIGHT,
		},

	};

	VkDescriptorPoolCreateInfo dp_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = array_count(sizes),
		.pPoolSizes = sizes,
		.maxSets = MAX_FRAMES_IN_FLIGHT,
	};

	if (vkCreateDescriptorPool(vk_state->device.logical, &dp_create_info, NULL, &vk_state->descriptor_pool) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan DescriptorPool");
		return false;
	}

	LOG_INFO("VkDescriptorPool created");
	return true;
}

bool vk_create_descriptor_set(VulkanState *vk_state) {
	VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		layouts[i] = vk_state->descriptor_set_layout;
	}

	VkDescriptorSetAllocateInfo ds_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vk_state->descriptor_pool,
		.descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
		.pSetLayouts = layouts
	};

	if (vkAllocateDescriptorSets(vk_state->device.logical, &ds_allocate_info, vk_state->descriptor_sets) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan DescriptorSets");
		return false;
	}

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		VkDescriptorBufferInfo buffer_info = {
			.buffer = vk_state->uniform_buffers[i],
			.offset = 0,
			.range = sizeof(MVPObject),
		};

		VkDescriptorImageInfo image_info = {
			.sampler = vk_state->texture_sampler,
			.imageView = vk_state->texture_image_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};

		VkWriteDescriptorSet descriptor_writes[] = {
			{
			  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			  .dstSet = vk_state->descriptor_sets[i],
			  .dstBinding = 0,
			  .dstArrayElement = 0,
			  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			  .descriptorCount = 1,
			  .pBufferInfo = &buffer_info,
			},
			{
			  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			  .dstSet = vk_state->descriptor_sets[i],
			  .dstBinding = 1,
			  .dstArrayElement = 0,
			  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  .descriptorCount = 1,
			  .pImageInfo = &image_info,
			},

		};

		vkUpdateDescriptorSets(vk_state->device.logical, array_count(descriptor_writes), descriptor_writes, 0, NULL);
	}

	LOG_INFO("Vulkan DescriptorSets created");
	return true;
}
