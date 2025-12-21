#include "core/debug.h"
#include "core/identifiers.h"
#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

#include "vk_internal.h"

#include <assert.h>
#include <spirv_reflect/spirv_reflect.h>

#include "common.h"
#include "core/logger.h"
#include "platform/filesystem.h"

#include "allocators/arena.h"
#include "renderer/vulkan/vk_types.h"

#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

bool create_shader_variant(VulkanContext *context, VulkanShader *shader, VulkanPipeline *variant, PipelineDesc description);
bool reflect_shader_interface(VulkanContext *context, VulkanShader *shader, FileContent vertex_shader_code, FileContent fragment_shader_code);

bool vulkan_renderer_create_shader(VulkanContext *context, uint32_t store_index, String vertex_shader_path, String fragment_shader_path, PipelineDesc description) {
	if (store_index >= MAX_SHADERS) {
		LOG_ERROR("Vulkan: Shader index %d out of bounds, aborting create", store_index);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[store_index];
	if (shader->vertex_shader != NULL || shader->fragment_shader != NULL) {
		ASSERT_FORMAT(false, "Engine: Frontend renderer tried to allocate shader at index %d, but index is already in use", store_index);
		return false;
	}

	ArenaTemp scratch = arena_scratch(NULL);

	FileContent vertex_shader_code = filesystem_read(scratch.arena, vertex_shader_path);
	FileContent fragment_shader_code = filesystem_read(scratch.arena, fragment_shader_path);

	if (vertex_shader_code.size <= 0 && fragment_shader_code.size <= 0) {
		LOG_ERROR("Failed to load files");
		arena_release_scratch(scratch);
		return false;
	}

	VkShaderModuleCreateInfo vsm_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = vertex_shader_code.size,
		.pCode = (uint32_t *)vertex_shader_code.content,
	};

	if (vkCreateShaderModule(context->device.logical, &vsm_create_info, NULL, &shader->vertex_shader) != VK_SUCCESS) {
		LOG_ERROR("Failed to create vertex shader module");
		return false;
	}

	VkShaderModuleCreateInfo fsm_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = fragment_shader_code.size,
		.pCode = (uint32_t *)fragment_shader_code.content,
	};

	if (vkCreateShaderModule(context->device.logical, &fsm_create_info, NULL, &shader->fragment_shader) != VK_SUCCESS) {
		LOG_ERROR("Failed to create fragment shader module");
		return false;
	}

	reflect_shader_interface(context, shader, vertex_shader_code, fragment_shader_code);
	create_shader_variant(context, shader, &shader->variants[0], description);

	arena_release_scratch(scratch);
	return true;
}

bool vulkan_renderer_destroy_shader(VulkanContext *context, uint32_t retrieve_index) {
	if (retrieve_index >= MAX_SHADERS) {
		LOG_ERROR("Vulkan: Shader index %d out of bounds", retrieve_index);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[retrieve_index];
	if (shader->vertex_shader == NULL || shader->fragment_shader == NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to destroy shader at index %d, but index is not in use", retrieve_index);
		ASSERT(false);
		return false;
	}

	vkDestroyShaderModule(context->device.logical, shader->vertex_shader, NULL);
	vkDestroyShaderModule(context->device.logical, shader->fragment_shader, NULL);

	shader->vertex_shader = NULL;
	shader->fragment_shader = NULL;

	vkDestroyDescriptorSetLayout(context->device.logical, shader->material_set_layout, NULL);
	vkDestroyPipelineLayout(context->device.logical, shader->pipeline_layout, NULL);

	for (uint32_t index = 0; index < shader->variant_count; ++index) {
		VulkanPipeline *pipeline = &shader->variants[index];

		vkDestroyPipeline(context->device.logical, pipeline->handle, NULL);
	}

	return true;
}

struct vertex_input_state {
	VkVertexInputAttributeDescription attributes[MAX_INPUT_ATTRIBUTES];
	VkVertexInputBindingDescription bindings[MAX_INPUT_BINDINGS];

	uint32_t binding_count, attribute_count;
};

static bool override_attributes(struct vertex_input_state *state, ShaderAttribute *attributes, uint32_t attribute_count);
bool create_shader_variant(VulkanContext *context, VulkanShader *shader, VulkanPipeline *variant, PipelineDesc description) {
	VkPipelineShaderStageCreateInfo vss_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = shader->vertex_shader,
		.pName = "main"
	};
	VkPipelineShaderStageCreateInfo fss_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = shader->fragment_shader,
		.pName = "main"
	};

	VkPipelineShaderStageCreateInfo shader_stages[] = { vss_create_info, fss_create_info };

	VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = countof(dynamic_states),
		.pDynamicStates = dynamic_states
	};

	VkPipelineVertexInputStateCreateInfo vis_create_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	struct vertex_input_state override = { 0 };
	if (description.override_count > 0) {
		override_attributes(&override, description.override_attributes, description.override_count);
		vis_create_info.vertexBindingDescriptionCount = override.binding_count;
		vis_create_info.pVertexBindingDescriptions = override.bindings;
		vis_create_info.vertexAttributeDescriptionCount = override.attribute_count;
		vis_create_info.pVertexAttributeDescriptions = override.attributes;
	} else {
		vis_create_info.vertexBindingDescriptionCount = shader->binding_count;
		vis_create_info.pVertexBindingDescriptions = shader->bindings;
		vis_create_info.vertexAttributeDescriptionCount = shader->attribute_count;
		vis_create_info.pVertexAttributeDescriptions = shader->attributes;
	}

	VkPipelineInputAssemblyStateCreateInfo ias_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = description.topology_line_list ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE
	};

	VkPipelineViewportStateCreateInfo vps_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkPipelineRasterizationStateCreateInfo rs_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = (VkPolygonMode)description.polygon_mode,
		.lineWidth = description.line_width > 0.0f ? description.line_width : 1.0f,
		.cullMode = (VkCullModeFlags)description.cull_mode,
		.frontFace = (VkFrontFace)description.front_face,
		.depthBiasEnable = VK_FALSE,
	};

	VkPipelineMultisampleStateCreateInfo mss_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.sampleShadingEnable = VK_FALSE,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.minSampleShading = 1.0f,
	};

	VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		.blendEnable = VK_FALSE,
	};

	VkPipelineColorBlendStateCreateInfo cbs_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attachment,
	};

	VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = description.depth_test_enable ? VK_TRUE : VK_FALSE,
		.depthWriteEnable = description.depth_write_enable ? VK_TRUE : VK_FALSE,
		.depthCompareOp = (VkCompareOp)description.depth_compare_op,
		.depthBoundsTestEnable = VK_FALSE,
		.minDepthBounds = 0.0f,
		.maxDepthBounds = 1.0f
	};

	VkPipelineRenderingCreateInfo r_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &context->swapchain.format.format,
		.depthAttachmentFormat = context->depth_attachment.format
	};

	VkGraphicsPipelineCreateInfo gp_create_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &r_create_info,
		.stageCount = countof(shader_stages),
		.pStages = shader_stages,
		.pVertexInputState = &vis_create_info,
		.pInputAssemblyState = &ias_create_info,
		.pViewportState = &vps_create_info,
		.pRasterizationState = &rs_create_info,
		.pMultisampleState = &mss_create_info,
		.pDepthStencilState = &depth_stencil_create_info,
		.pColorBlendState = &cbs_create_info,
		.pDynamicState = &ds_create_info,
		.layout = shader->pipeline_layout,
	};

	if (vkCreateGraphicsPipelines(context->device.logical, VK_NULL_HANDLE, 1, &gp_create_info, NULL, &variant->handle) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: Failed to create pipeline");
		return false;
	}

	variant->description = description;
	shader->variant_count++;

	LOG_INFO("Vulkan: VkPipeline created");
	return true;
}

bool vulkan_renderer_bind_shader(VulkanContext *context, uint32_t shader_index, uint32_t resource_index) {
	if (shader_index >= MAX_SHADERS || resource_index >= MAX_RESOURCE_SETS) {
		LOG_ERROR("Vulkan: indices [shader_index = %d, resource_index = %d] out of bounds", shader_index, resource_index);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[shader_index];
	VulkanPipeline *pipeline = &shader->variants[0];
	if (pipeline->handle == NULL) {
		ASSERT_FORMAT(false, "Engine: Frontend renderer tried to bind shader at index %d, but index is not in use", shader_index);
		return false;
	}

	vkCmdBindPipeline(context->command_buffers[context->current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);
	vkCmdBindDescriptorSets(
		context->command_buffers[context->current_frame],
		VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline_layout,
		SHADER_UNIFORM_FREQUENCY_PER_FRAME, 1, &context->global_set.sets[context->current_frame], 0, NULL);
	vkCmdBindDescriptorSets(
		context->command_buffers[context->current_frame],
		VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline_layout,
		SHADER_UNIFORM_FREQUENCY_PER_MATERIAL, 1, &shader->material_sets[resource_index].sets[context->current_frame], 0, NULL);

	return true;
}

bool vulkan_renderer_create_shader_resource(VulkanContext *context, uint32_t store_index, uint32_t shader_index) {
	if (store_index >= MAX_RESOURCE_SETS || shader_index >= MAX_SHADERS) {
		LOG_ERROR("Vulkan: indices [store_index = %d, shader_index = %d] out of bounds", store_index, shader_index);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[shader_index];
	if (shader->vertex_shader == NULL || shader->fragment_shader == NULL) {
		ASSERT_FORMAT(false, "Vulkan: Frontend renderer tried to allocate resource set with shader at index %d, but index is not in use", shader_index);
		return false;
	}

	VulkanResourceSet *resource_set = &shader->material_sets[store_index];
	if (resource_set->sets[0] != NULL) {
		ASSERT_FORMAT(false, "Vulkan: Frontend renderer tried to allocate resource set at index %d, but index is already in use", store_index);
		return false;
	}

	VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
		layouts[frame] = shader->material_set_layout;
	}

	VkDescriptorSetAllocateInfo ds_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = context->descriptor_pool,
		.descriptorSetCount = countof(layouts),
		.pSetLayouts = layouts
	};

	if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, resource_set->sets) != VK_SUCCESS) {
		LOG_ERROR("Failed to create Vulkan DescriptorSets");
		return false;
	}

	LOG_INFO("Vulkan: VkDescriptorSet created");

	return true;
}

bool vulkan_renderer_set_shader_resource_buffer(VulkanContext *context, uint32_t shader_index, uint32_t resource_index, String name, uint32_t buffer_index) {
	if (shader_index >= MAX_SHADERS) {
		LOG_ERROR("Vulkan: Shader index %d out of bounds, aborting", shader_index);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[shader_index];
	if (shader->vertex_shader == NULL || shader->fragment_shader == NULL) {
		ASSERT_FORMAT(false, "Vulkan: Shader at index %d is not in use", shader_index);
		return false;
	}

	VulkanBuffer *uniform_buffer = &context->buffer_pool[buffer_index];
	if (uniform_buffer->handle[0] == NULL) {
		ASSERT_FORMAT(false, "Vulkan: Buffer target at index %d is not in use", buffer_index);
		return false;
	}

	ShaderUniform *target = NULL;
	for (uint32_t index = 0; index < MAX_UNIFORMS; ++index) {
		if (string_equals(string_create(shader->uniforms[index].name), name)) {
			target = &shader->uniforms[index];
			LOG_DEBUG("Vulkan: Uniform buffer '%.*s' found. Set = %d, Binding = %d", FS(name), target->set, target->binding);
		}
	}

	if (target == NULL) {
		LOG_WARN("Vulkan: Uniform buffer '%.*s' not found in shader %d", FS(name), shader_index);
		return false;
	}

	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
		VkDescriptorBufferInfo buffer_info = {
			.buffer = uniform_buffer->handle[frame],
			.offset = 0,
			.range = VK_WHOLE_SIZE,
		};

		VkWriteDescriptorSet descriptor_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = shader->material_sets[resource_index].sets[frame],
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

bool vulkan_renderer_set_shader_resource_texture_sampler(VulkanContext *context, uint32_t shader_index, uint32_t resource_index, String name, uint32_t texture_index, uint32_t sampler_index) {
	if (shader_index >= MAX_SHADERS || texture_index >= MAX_TEXTURES || sampler_index >= MAX_SAMPLERS) {
		LOG_ERROR("Vulkan: one or more of [shader_index = %d, texture_index = %d, sampler_index = %d] out of bounds", shader_index, texture_index, sampler_index);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[shader_index];
	if (shader->vertex_shader == NULL || shader->fragment_shader == NULL) {
		ASSERT_FORMAT(false, "Vulkan: Shader at index %d is not in use", shader_index);
		return false;
	}

	VulkanImage *texture = &context->image_pool[texture_index];
	if (texture->handle == NULL) {
		ASSERT_FORMAT(false, "Engine: Frontend renderer tried to update resource set texture to texture at index %d, but texture index is not in use", texture_index);
		return false;
	}

	VulkanSampler *sampler = &context->sampler_pool[sampler_index];
	if (sampler->handle == NULL) {
		ASSERT_FORMAT(false, "Engine: Frontend renderer tried to update resource set sampler to sampler at index %d, but sampler index is not in use", sampler_index);
		return false;
	}

	ShaderUniform *target = NULL;
	for (uint32_t index = 0; index < MAX_UNIFORMS; ++index) {
		if (string_equals(string_create(shader->uniforms[index].name), name) && shader->uniforms[index].type == SHADER_UNIFORM_TYPE_COMBINED_IMAGE_SAMPLER) {
			target = &shader->uniforms[index];
			LOG_DEBUG("Vulkan: Uniform texture sampler '%.*s' found", FS(name));
		}
	}

	if (target == NULL) {
		LOG_WARN("Vulkan: Uniform texture sampler '%.*s' not found in shader %d", FS(name), shader_index);
		return false;
	}

	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
		VkDescriptorImageInfo image_info = {
			.sampler = sampler->handle,
			.imageView = texture->view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};

		VkWriteDescriptorSet descriptor_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = shader->material_sets[resource_index].sets[frame],
			.dstBinding = target->binding,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.pImageInfo = &image_info,
		};

		vkUpdateDescriptorSets(context->device.logical, 1, &descriptor_write, 0, NULL);
	}

	return true;
}

bool reflect_shader_interface(VulkanContext *context, VulkanShader *shader, FileContent vertex_shader_code, FileContent fragment_shader_code) {
	SpvReflectShaderModule vertex_module, fragment_module;
	SpvReflectResult result;

	// Create reflection modules
	result = spvReflectCreateShaderModule(vertex_shader_code.size, vertex_shader_code.content, &vertex_module);
	if (result != SPV_REFLECT_RESULT_SUCCESS) {
		LOG_ERROR("Failed to reflect vertex shader");
		return false;
	}

	result = spvReflectCreateShaderModule(fragment_shader_code.size, fragment_shader_code.content, &fragment_module);
	if (result != SPV_REFLECT_RESULT_SUCCESS) {
		LOG_ERROR("Failed to reflect fragment shader");
		spvReflectDestroyShaderModule(&vertex_module);
		return false;
	}

	ArenaTemp scratch = arena_scratch(NULL);

	// ========== VERTEX INPUT ATTRIBUTES ==========
	uint32_t input_variable_count = 0;
	result = spvReflectEnumerateInputVariables(&vertex_module, &input_variable_count, NULL);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

	SpvReflectInterfaceVariable **input_variables = arena_push_array(scratch.arena, SpvReflectInterfaceVariable *, input_variable_count);
	result = spvReflectEnumerateInputVariables(&vertex_module, &input_variable_count, input_variables);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

	shader->attribute_count = 0;
	shader->binding_count = 0;

	// Track unique bindings
	uint32_t bindings_used[MAX_INPUT_BINDINGS] = { 0 };
	uint32_t max_attributes = context->device.properties.limits.maxVertexInputAttributes;
	uint32_t *indices = arena_push_array_zero(scratch.arena, uint32_t, max_attributes);
	memset(indices, INT32_MAX, sizeof(uint32_t) * max_attributes);

	for (uint32_t index = 0; index < input_variable_count; ++index) {
		SpvReflectInterfaceVariable *var = input_variables[index];

		// Skip built-ins (gl_VertexIndex, etc.)
		if (var->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) {
			continue;
		}

		VkVertexInputAttributeDescription *attr = &shader->attributes[index];
		indices[var->location] = index;
		attr->location = var->location;
		attr->binding = 0; // Assume single binding for now
		attr->format = (VkFormat)var->format;
		attr->offset = 0; // Will be calculated based on previous attributes

		shader->attribute_count++;
		bindings_used[0] = 1; // Mark binding 0 as used

		ASSERT(shader->attribute_count < MAX_INPUT_ATTRIBUTES);
	}

	// Create binding descriptions for used bindings
	uint32_t stride = 0;
	for (uint32_t index = 0; index < shader->attribute_count; ++index) {
		uint32_t next_index;
		for (uint32_t attribute_index = 0; attribute_index < max_attributes; ++attribute_index) {
			if ((next_index = indices[attribute_index]) != UINT32_MAX) {
				indices[attribute_index] = -1;
				break;
			}
		}

		if (next_index == UINT32_MAX)
			break;

		VkVertexInputAttributeDescription *attr = &shader->attributes[next_index];
		attr->offset += stride;

		// Calculate stride based on format
		VkFormat format = attr->format;
		if (format == VK_FORMAT_R32_SFLOAT)
			stride += 4;
		else if (format == VK_FORMAT_R32G32_SFLOAT)
			stride += 8;
		else if (format == VK_FORMAT_R32G32B32_SFLOAT)
			stride += 12;
		else if (format == VK_FORMAT_R32G32B32A32_SFLOAT)
			stride += 16;
		// Add more formats as needed
	}

	if (bindings_used[0]) {
		shader->bindings[0].binding = 0;
		shader->bindings[0].stride = stride;
		shader->bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		shader->binding_count = 1;
	}

	// ========== DESCRIPTOR SETS ==========
	uint32_t vs_set_count = 0, fs_set_count = 0;
	result = spvReflectEnumerateDescriptorSets(&vertex_module, &vs_set_count, NULL);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);
	result = spvReflectEnumerateDescriptorSets(&fragment_module, &fs_set_count, NULL);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

	SpvReflectDescriptorSet **vs_sets = arena_push_array(scratch.arena, SpvReflectDescriptorSet *, vs_set_count);
	SpvReflectDescriptorSet **fs_sets = arena_push_array(scratch.arena, SpvReflectDescriptorSet *, fs_set_count);

	result = spvReflectEnumerateDescriptorSets(&vertex_module, &vs_set_count, vs_sets);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);
	result = spvReflectEnumerateDescriptorSets(&fragment_module, &fs_set_count, fs_sets);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

	// Merge descriptor sets from both stages
	typedef struct {
		uint32_t set;
		uint32_t binding_count;
		VkDescriptorSetLayoutBinding bindings[32];
	} SetInfo;

	SetInfo merged_sets[MAX_SETS] = { 0 };
	uint32_t unique_set_count = 0;

	shader->uniform_count = 0;

	// Process vertex shader sets
	for (uint32_t i = 0; i < vs_set_count; ++i) {
		SpvReflectDescriptorSet *reflect_set = vs_sets[i];
		SetInfo *info = &merged_sets[reflect_set->set];
		info->set = reflect_set->set;

		for (uint32_t b = 0; b < reflect_set->binding_count; ++b) {
			SpvReflectDescriptorBinding *reflect_binding = reflect_set->bindings[b];

			VkDescriptorSetLayoutBinding *vk_binding = &info->bindings[info->binding_count];
			vk_binding->binding = reflect_binding->binding;
			vk_binding->descriptorType = (VkDescriptorType)reflect_binding->descriptor_type;
			vk_binding->descriptorCount = reflect_binding->count;
			vk_binding->stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			vk_binding->pImmutableSamplers = NULL;

			ShaderUniform *uniform = &shader->uniforms[shader->uniform_count++];

			memcpy(uniform->name, reflect_binding->name, sizeof(uniform->name));
			uniform->type =
				reflect_binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER
				? SHADER_UNIFORM_TYPE_BUFFER
				: reflect_binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
				? SHADER_UNIFORM_TYPE_COMBINED_IMAGE_SAMPLER
				: SHADER_UNIFORM_UNDEFINED;
			uniform->stage = SHADER_STAGE_VERTEX;
			uniform->count = reflect_binding->count;
			uniform->set = reflect_binding->set;
			uniform->binding = reflect_binding->binding;
			if (uniform->type == SHADER_UNIFORM_TYPE_BUFFER) {
				SpvReflectBlockVariable block = reflect_binding->block;

				uniform->size = block.size;
			}

			info->binding_count++;
		}

		if (reflect_set->set >= unique_set_count) {
			unique_set_count = reflect_set->set + 1;
		}
	}

	// Process fragment shader sets (merge or add new)
	for (uint32_t i = 0; i < fs_set_count; ++i) {
		SpvReflectDescriptorSet *reflect_set = fs_sets[i];
		SetInfo *info = &merged_sets[reflect_set->set];

		if (info->binding_count == 0) {
			info->set = reflect_set->set;
		}

		for (uint32_t b = 0; b < reflect_set->binding_count; ++b) {
			SpvReflectDescriptorBinding *reflect_binding = reflect_set->bindings[b];

			bool found_uniform = false;
			for (uint32_t uniform_index = 0; uniform_index < shader->uniform_count; ++uniform_index) {
				ShaderUniform *uniform = &shader->uniforms[uniform_index];
				if (uniform->set == reflect_set->set && uniform->binding == reflect_binding->binding) {
					uniform->stage |= SHADER_STAGE_FRAGMENT;
					found_uniform = true;
				}
			}

			// Check if binding already exists (from vertex shader)
			bool found = false;
			for (uint32_t existing = 0; existing < info->binding_count; ++existing) {
				if (info->bindings[existing].binding == reflect_binding->binding) {
					// Merge stage flags
					info->bindings[existing].stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
					found = true;

					break;
				}
			}

			if (found_uniform == false) {
				ShaderUniform *uniform = &shader->uniforms[shader->uniform_count++];
				memcpy(uniform->name, reflect_binding->name, sizeof(uniform->name));
				uniform->type =
					reflect_binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER
					? SHADER_UNIFORM_TYPE_BUFFER
					: reflect_binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
					? SHADER_UNIFORM_TYPE_COMBINED_IMAGE_SAMPLER
					: SHADER_UNIFORM_UNDEFINED;
				uniform->stage = SHADER_STAGE_FRAGMENT;
				uniform->count = reflect_binding->count;
				uniform->set = reflect_binding->set;
				uniform->binding = reflect_binding->binding;
			}

			if (!found) {
				VkDescriptorSetLayoutBinding *vk_binding = &info->bindings[info->binding_count];
				vk_binding->binding = reflect_binding->binding;
				vk_binding->descriptorType = (VkDescriptorType)reflect_binding->descriptor_type;
				vk_binding->descriptorCount = reflect_binding->count;
				vk_binding->stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
				vk_binding->pImmutableSamplers = NULL;

				info->binding_count++;
			}
		}

		if (reflect_set->set >= unique_set_count) {
			unique_set_count = reflect_set->set + 1;
		}
	}

	SetInfo *global_reflect_info = &merged_sets[SHADER_UNIFORM_FREQUENCY_PER_FRAME];
	if (global_reflect_info->binding_count != context->global_set.binding_count) {
		spvReflectDestroyShaderModule(&vertex_module);
		spvReflectDestroyShaderModule(&fragment_module);
		arena_release_scratch(scratch);
		LOG_ERROR("Vulkan: Shader is incompatible, aborting");
		return false;
	}

	for (uint32_t binding_index = 0; binding_index < global_reflect_info->binding_count; ++binding_index) {
		VkDescriptorSetLayoutBinding *binding = &global_reflect_info->bindings[binding_index];

		if (binding->descriptorType != context->global_set.bindings[binding_index].descriptorType) {
			LOG_ERROR("Vulkan: Shader is incompatible, aborting");
			spvReflectDestroyShaderModule(&vertex_module);
			spvReflectDestroyShaderModule(&fragment_module);
			arena_release_scratch(scratch);
			return false;
		}
	}

	SetInfo *material_reflect_info = &merged_sets[SHADER_UNIFORM_FREQUENCY_PER_MATERIAL];
	if (vulkan_create_descriptor_set_layout(context, material_reflect_info->bindings, material_reflect_info->binding_count, &shader->material_set_layout) == false) {
		spvReflectDestroyShaderModule(&vertex_module);
		spvReflectDestroyShaderModule(&fragment_module);
		arena_release_scratch(scratch);
		return false;
	}

	VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
	for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
		layouts[frame] = shader->material_set_layout;
	}

	// VkDescriptorSetAllocateInfo ds_allocate_info = {
	// 	.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
	// 	.descriptorPool = context->descriptor_pool,
	// 	.descriptorSetCount = countof(layouts),
	// 	.pSetLayouts = layouts
	// };
	//
	// if (vkAllocateDescriptorSets(context->device.logical, &ds_allocate_info, shader->material_set.sets) != VK_SUCCESS) {
	// 	LOG_ERROR("Vulkan: Failed to create VkDescriptorSets for shader");
	// 	return false;
	// }
	//
	// LOG_INFO("Vulkan: Shader VkDescriptorSets created");

	// ========== PUSH CONSTANTS ==========
	uint32_t vs_push_count = 0, fs_push_count = 0;
	result = spvReflectEnumeratePushConstantBlocks(&vertex_module, &vs_push_count, NULL);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);
	result = spvReflectEnumeratePushConstantBlocks(&fragment_module, &fs_push_count, NULL);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

	shader->ps_count = 0;

	uint32_t push_constant_offset = shader->uniform_count;

	if (vs_push_count > 0) {
		SpvReflectBlockVariable **push_blocks = arena_push_array(scratch.arena, SpvReflectBlockVariable *, vs_push_count);
		result = spvReflectEnumeratePushConstantBlocks(&vertex_module, &vs_push_count, push_blocks);
		ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

		for (uint32_t i = 0; i < vs_push_count; ++i) {
			SpvReflectBlockVariable *push_block = push_blocks[i];

			shader->push_constant_ranges[shader->ps_count].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			shader->push_constant_ranges[shader->ps_count].offset = push_block->offset;
			shader->push_constant_ranges[shader->ps_count].size = push_block->size;
			shader->ps_count++;

			ShaderUniform *uniform = &shader->uniforms[shader->uniform_count++];
			memcpy(uniform->name, push_block->name, sizeof(uniform->name));
			uniform->type = SHADER_UNIFORM_TYPE_CONSTANTS;
			uniform->stage = SHADER_STAGE_VERTEX;
			uniform->size = push_block->size;
		}
	}

	if (fs_push_count > 0) {
		SpvReflectBlockVariable **push_blocks = arena_push_array(scratch.arena, SpvReflectBlockVariable *, fs_push_count);
		result = spvReflectEnumeratePushConstantBlocks(&fragment_module, &fs_push_count, push_blocks);
		ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

		for (uint32_t i = 0; i < fs_push_count; ++i) {
			SpvReflectBlockVariable *push_block = push_blocks[i];

			bool found_uniform = false;

			for (uint32_t uniform_index = push_constant_offset; uniform_index < shader->uniform_count; ++uniform_index) {
				ShaderUniform *uniform = &shader->uniforms[uniform_index];
				if (strcmp(uniform->name, push_block->name) == 0) {
					uniform->stage |= SHADER_STAGE_FRAGMENT;
					found_uniform = true;
				}
			}

			// Check if range already exists (merge stage flags)
			bool found = false;
			for (uint32_t j = 0; j < shader->ps_count; ++j) {
				if (shader->push_constant_ranges[j].offset == push_block->offset &&
					shader->push_constant_ranges[j].size == push_block->size) {
					shader->push_constant_ranges[j].stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
					found = true;
					break;
				}
			}

			if (found_uniform == false) {
				ShaderUniform *uniform = &shader->uniforms[shader->uniform_count++];
				memcpy(uniform->name, push_block->name, sizeof(uniform->name));
				uniform->type = SHADER_UNIFORM_TYPE_CONSTANTS;
				uniform->stage = SHADER_STAGE_FRAGMENT;
				uniform->size = push_block->size;
			}

			if (!found) {
				shader->push_constant_ranges[shader->ps_count].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
				shader->push_constant_ranges[shader->ps_count].offset = push_block->offset;
				shader->push_constant_ranges[shader->ps_count].size = push_block->size;
				shader->ps_count++;
			}
		}
	}

	spvReflectDestroyShaderModule(&vertex_module);
	spvReflectDestroyShaderModule(&fragment_module);
	arena_release_scratch(scratch);

	// ========== CREATE PIPELINE LAYOUT ==========
	layouts[SHADER_UNIFORM_FREQUENCY_PER_FRAME] = context->globa_set_layout;
	layouts[SHADER_UNIFORM_FREQUENCY_PER_MATERIAL] = shader->material_set_layout;

	VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 2,
		.pSetLayouts = layouts,
		.pushConstantRangeCount = shader->ps_count,
		.pPushConstantRanges = shader->push_constant_ranges,
	};

	if (vkCreatePipelineLayout(context->device.logical, &pipeline_layout_info, NULL, &shader->pipeline_layout) != VK_SUCCESS) {
		LOG_ERROR("Failed to create pipeline layout");
		return false;
	}
	return true;
}

static inline VkFormat to_vk_format(ShaderAttributeFormat format);
static inline size_t to_bytes(ShaderAttributeFormat format);

static bool override_attributes(struct vertex_input_state *override, ShaderAttribute *attributes, uint32_t attribute_count) {
	uint32_t unique_bindings = 0;
	for (uint32_t attribute_index = 0; attribute_index < attribute_count; ++attribute_index) {
		ShaderAttribute attribute = attributes[attribute_index];
		uint32_t byte_offset = 0;
		for (uint32_t binding_index = 0; binding_index < attribute_count; ++binding_index) {
			VkVertexInputBindingDescription *binding_description = &override->bindings[binding_index];
			unique_bindings += binding_description->stride == 0;

			if (binding_description->binding == attribute.binding || binding_description->stride == 0) {
				binding_description->binding = attribute.binding;
				binding_description->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
				byte_offset = binding_description->stride;
				binding_description->stride += to_bytes(attribute.format);
				break;
			}
		}

		VkVertexInputAttributeDescription attribute_description = {
			.binding = attribute.binding,
			.location = attribute_index,
			.format = to_vk_format(attribute.format),
			.offset = byte_offset
		};

		override->attributes[attribute_index] = attribute_description;
	}

	override->binding_count = unique_bindings;
	override->attribute_count = attribute_count;

	return true;
}

VkFormat to_vk_format(ShaderAttributeFormat format) {
	switch (format.type) {
		case SHADER_ATTRIBUTE_TYPE_FLOAT64: {
			const VkFormat types[] = { VK_FORMAT_R64_SFLOAT, VK_FORMAT_R64G64_SFLOAT, VK_FORMAT_R64G64B64_SFLOAT, VK_FORMAT_R64G64B64A64_SFLOAT };
			return types[format.count - 1];
		}
		case SHADER_ATTRIBUTE_TYPE_INT64: {
			const VkFormat types[] = { VK_FORMAT_R64_SINT, VK_FORMAT_R64G64_SINT, VK_FORMAT_R64G64B64_SINT, VK_FORMAT_R64G64B64A64_SINT };
			return types[format.count - 1];
		}
		case SHADER_ATTRIBUTE_TYPE_UINT64: {
			const VkFormat types[] = { VK_FORMAT_R64_UINT, VK_FORMAT_R64G64_UINT, VK_FORMAT_R64G64B64_UINT, VK_FORMAT_R64G64B64A64_UINT };
			return types[format.count - 1];
		}
		case SHADER_ATTRIBUTE_TYPE_FLOAT32: {
			const VkFormat types[] = { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT };
			return types[format.count - 1];
		}
		case SHADER_ATTRIBUTE_TYPE_INT32: {
			const VkFormat types[] = { VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32A32_SINT };
			return types[format.count - 1];
		}
		case SHADER_ATTRIBUTE_TYPE_UINT32: {
			const VkFormat types[] = { VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32A32_UINT };
			return types[format.count - 1];
		}
		case SHADER_ATTRIBUTE_TYPE_INT16: {
			const VkFormat types[] = { VK_FORMAT_R16_SINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16A16_SINT };
			return types[format.count - 1];
		}
		case SHADER_ATTRIBUTE_TYPE_UINT16: {
			const VkFormat types[] = { VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16A16_UINT };
			return types[format.count - 1];
		}
		case SHADER_ATTRIBUTE_TYPE_INT8: {
			const VkFormat types[] = { VK_FORMAT_R8_SINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8B8_SINT, VK_FORMAT_R8G8B8A8_SINT };
			return types[format.count - 1];
		}
		case SHADER_ATTRIBUTE_TYPE_UINT8: {
			const VkFormat types[] = { VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8A8_UINT };
			return types[format.count - 1];
		}
		default: {
			ASSERT_MESSAGE(false, "Shader attribute type should not be undefined");
			return VK_FORMAT_UNDEFINED;
		}
	}
}
size_t to_bytes(ShaderAttributeFormat format) {
	size_t type_size = 0;
	switch (format.type) {
		case SHADER_ATTRIBUTE_TYPE_FLOAT64:
		case SHADER_ATTRIBUTE_TYPE_INT64:
		case SHADER_ATTRIBUTE_TYPE_UINT64:
			type_size = 8;
			break;
		case SHADER_ATTRIBUTE_TYPE_FLOAT32:
		case SHADER_ATTRIBUTE_TYPE_INT32:
		case SHADER_ATTRIBUTE_TYPE_UINT32:
			type_size = 4;
			break;
		case SHADER_ATTRIBUTE_TYPE_INT16:
		case SHADER_ATTRIBUTE_TYPE_UINT16:
			type_size = 2;
			break;
		case SHADER_ATTRIBUTE_TYPE_INT8:
		case SHADER_ATTRIBUTE_TYPE_UINT8:
			type_size = 1;
			break;
		case SHADER_ATTRIBUTE_TYPE_UNDEFINED:
		case SHADER_ATTRIBUTE_TYPE_LAST:
		default:
			return 0;
	}
	return type_size * format.count;
}
