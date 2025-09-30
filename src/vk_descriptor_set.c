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
