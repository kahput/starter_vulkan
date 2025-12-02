#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

#include "allocators/pool.h"
#include "common.h"
#include "core/logger.h"
#include <string.h>

bool vulkan_renderer_create_resource_set(VulkanContext *context, uint32_t store_index, uint32_t shader_index, uint32_t set_number) {
	if (store_index >= MAX_RESOURCE_SETS || shader_index >= MAX_SHADERS) {
		LOG_ERROR("Vulkan: indices [store_index = %d, shader_index = %d] out of bounds", store_index, shader_index);
		return false;
	}

	VulkanResourceSet *resource_set = &context->set_pool[store_index];
	if (resource_set->sets[0] != NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to allocate resource set at index %d, but index is already in use", store_index);
		assert(false);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[shader_index];
	if (shader->vertex_shader == NULL || shader->fragment_shader == NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to allocateresource set with shader at index %d, but index is not in use", shader_index);
		assert(false);
		return false;
	}

	resource_set->shader_index = shader_index;

	VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
		layouts[frame] = shader->layouts[set_number];
	}

	VkDescriptorSetAllocateInfo ds_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = context->descriptor_pool,
		.descriptorSetCount = array_count(layouts),
		.pSetLayouts = layouts
	};

	if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, resource_set->sets) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan DescriptorSets");
		return false;
	}

	LOG_INFO("Vulkan: VkDescriptorSet created");

	return true;
}

bool vulkan_renderer_update_resource_set_buffer(VulkanContext *context, uint32_t set_index, const char *name, uint32_t buffer_index) {
	VulkanResourceSet *resource_set = &context->set_pool[set_index];
	if (resource_set->sets[0] == NULL) {
		LOG_FATAL("Vulkan: Renderer requested resource set update at index %d, but no valid set found at index", set_index);
		assert(false);
		return false;
	}

	VulkanBuffer *uniform_buffer = &context->buffer_pool[buffer_index];
	if (uniform_buffer->handle[0] == NULL) {
		LOG_FATAL("Vulkan: Renderer requested resource set buffer update at index %d, but no valid buffer found at index", buffer_index);
		assert(false);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[resource_set->shader_index];

	ShaderUniform *target = NULL;
	for (uint32_t index = 0; index < MAX_UNIFORMS; ++index) {
		if (strcmp(shader->uniforms[index].name, name) == 0) {
			target = &shader->uniforms[index];
		}
	}

	if (target == NULL) {
		LOG_WARN("Vulkan: Uniform buffer '%s' not found in shader %d", name, resource_set->shader_index);
		return false;
	}

	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
		VkDescriptorBufferInfo buffer_info = {
			.buffer = uniform_buffer->handle[frame],
			.offset = 0,
			.range = VK_WHOLE_SIZE,
		};

		// VulkanImage *texture = &context->texture_pool[0];
		// VulkanSampler *sampler = &context->sampler_pool[0];
		//
		// VkDescriptorImageInfo image_info = {
		// 	.sampler = sampler->handle,
		// 	.imageView = texture->view,
		// 	.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		// };

		VkWriteDescriptorSet descriptor_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = resource_set->sets[frame],
			.dstBinding = target->binding,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = target->count,
			.pBufferInfo = &buffer_info,
		};

		vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);
	}

	return true;
}

bool vulkan_renderer_update_resource_set_texture_sampler(VulkanContext *context, uint32_t set_index, const char *name, uint32_t texture_index, uint32_t sampler_index) {
	if (set_index >= MAX_RESOURCE_SETS || texture_index >= MAX_TEXTURES || sampler_index >= MAX_SAMPLERS) {
		LOG_ERROR("Vulkan: one or more of [set_index = %d, texture_index = %d, sampler_index = %d] out of bounds", set_index, texture_index, sampler_index);
		return false;
	}

	VulkanResourceSet *resource_set = &context->set_pool[set_index];
	if (resource_set->sets[0] == NULL) {
		LOG_FATAL("Vulkan: Renderer requested resource set update at index %d, but no valid set found at index", set_index);
		assert(false);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[resource_set->shader_index];

	VulkanImage *texture = &context->texture_pool[texture_index];
	if (texture->handle == NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to update resource set texture to texture at index %d, but texture index is not in use", texture_index);
		assert(false);
		return false;
	}

	VulkanSampler *sampler = &context->sampler_pool[sampler_index];
	if (sampler->handle == NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to update resource set sampler to sampler at index %d, but sampler index is not in use", sampler_index);
		assert(false);
		return false;
	}

	ShaderUniform *target = NULL;
	for (uint32_t index = 0; index < MAX_UNIFORMS; ++index) {
		if (strcmp(shader->uniforms[index].name, name) == 0 && shader->uniforms[index].type == SHADER_UNIFORM_TYPE_COMBINED_IMAGE_SAMPLER) {
			target = &shader->uniforms[index];
			LOG_DEBUG("Vulkan: Uniform texture sampler '%s' found", name);
		}
	}

	if (target == NULL) {
		LOG_WARN("Vulkan: Uniform texture sampler '%s' not found in shader %d", name, resource_set->shader_index);
	}

	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
		VkDescriptorImageInfo image_info = {
			.sampler = sampler->handle,
			.imageView = texture->view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};

		VkWriteDescriptorSet descriptor_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = resource_set->sets[frame],
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.pImageInfo = &image_info,
		};

		vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);
	}

	return true;
}

bool vulkan_renderer_bind_resource_set(VulkanContext *context, uint32_t retrieve_index) {
	if (retrieve_index >= MAX_RESOURCE_SETS) {
		LOG_ERROR("Vulkan: Resource set index %d out of bounds", retrieve_index);
		return false;
	}

	VulkanResourceSet *set = &context->set_pool[retrieve_index];
	if (set->sets[0] == NULL) {
		LOG_FATAL("Vulkan: Renderer requested resource set bind at index %d, but no valid set found at index", retrieve_index);
		assert(false);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[set->shader_index];
	vkCmdBindDescriptorSets(
		context->command_buffers[context->current_frame],
		VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline_layout,
		0, 1, &set->sets[context->current_frame], 0, NULL);
	return true;
}

bool vulkan_create_descriptor_set_layout(VulkanContext *context, VkDescriptorSetLayoutBinding *bindings, uint32_t binding_count, VkDescriptorSetLayout *out_layout) {
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

bool vulkan_create_descriptor_pool(VulkanContext *context) {
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
		.poolSizeCount = array_count(sizes),
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

// bool vulkan_create_descriptor_set(VulkanContext *context, VulkanShader *shader) {
// 	VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
// 	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
// 		layouts[frame] = shader->layouts[0];
// 	}
//
// 	VkDescriptorSetAllocateInfo ds_allocate_info = {
// 		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
// 		.descriptorPool = shader->descriptor_pool,
// 		.descriptorSetCount = array_count(layouts),
// 		.pSetLayouts = layouts
// 	};
//
// 	if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, shader->descriptor_sets) != VK_SUCCESS) {
// 		LOG_ERROR("Failed to create Vulkan DescriptorSets");
// 		return false;
// 	}
//
// 	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
// 		VkDescriptorBufferInfo buffer_info = {
// 			.buffer = shader->uniform_buffers[frame].handle,
// 			.offset = 0,
// 			.range = sizeof(MVPObject),
// 		};
//
// 		// VulkanImage *texture = &context->texture_pool[0];
// 		// VulkanSampler *sampler = &context->sampler_pool[0];
// 		//
// 		// VkDescriptorImageInfo image_info = {
// 		// 	.sampler = sampler->handle,
// 		// 	.imageView = texture->view,
// 		// 	.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
// 		// };
//
// 		VkWriteDescriptorSet descriptor_write = {
// 			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
// 			.dstSet = shader->descriptor_sets[frame],
// 			.dstBinding = 0,
// 			.dstArrayElement = 0,
// 			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
// 			.descriptorCount = 1,
// 			.pBufferInfo = &buffer_info,
// 		};
//
// 		vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);
// 	}
//
// 	LOG_INFO("Vulkan DescriptorSets created");
// 	return true;
// }
//
// bool vulkan_renderer_set_uniform_buffer(VulkanContext *context, uint32_t shader_index, const char *name, void *data) {
// 	if (shader_index >= MAX_SHADERS) {
// 		LOG_ERROR("Vulkan: Shader index %d out of bounds", shader_index);
// 		return false;
// 	}
//
// 	VulkanShader *shader = &context->shader_pool[shader_index];
// 	if (shader->vertex_shader == NULL || shader->fragment_shader == NULL) {
// 		LOG_FATAL("Engine: Frontend renderer tried to set uniforms for shader at index %d, but index is not in use", shader_index);
// 		assert(false);
// 		return false;
// 	}
//
// 	ShaderUniform *target = NULL;
// 	for (uint32_t index = 0; index < MAX_UNIFORMS; ++index) {
// 		if (strcmp(shader->uniforms[index].name, name) == 0) {
// 			target = &shader->uniforms[index];
// 		}
// 	}
//
// 	if (target == NULL) {
// 		LOG_WARN("Vulkan: Uniform '%s' not found in shader %d", name, shader_index);
// 		return false;
// 	}
//
// 	VulkanBuffer *buffer = &shader->uniform_buffers[context->current_frame];
// 	uint8_t *dest = (uint8_t *)buffer->mapped;
// 	memcpy(dest, data, target->size);
//
// 	return true;
// }
//
