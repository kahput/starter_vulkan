#include "core/identifiers.h"
#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

#include "vk_internal.h"

#include <assert.h>
#include <spirv_reflect/spirv_reflect.h>

#include "common.h"
#include "core/logger.h"

#include "allocators/arena.h"
#include "renderer/vulkan/vk_types.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

struct shader_file {
	uint32_t size;
	uint8_t *content;
};

// static inline int32_t ftovkf(ShaderAttributeFormat format);
// static inline uint32_t ftobs(ShaderAttributeFormat format);

struct shader_file read_file(struct arena *arena, const char *path);
bool reflect_shader_interface(VulkanContext *context, VulkanShader *shader, struct shader_file vertex_shader_code, struct shader_file fragment_shader_code);

bool vulkan_renderer_create_shader(VulkanContext *context, uint32_t store_index, const char *vertex_shader_path, const char *fragment_shader_path) {
	if (store_index >= MAX_SHADERS) {
		LOG_ERROR("Vulkan: Shader index %d out of bounds", store_index);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[store_index];
	if (shader->vertex_shader != NULL || shader->fragment_shader != NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to allocateshader at index %d, but index is already in use", store_index);
		assert(false);
		return false;
	}

	ArenaTemp temp = arena_get_scratch(NULL);
	struct shader_file vertex_shader_code = read_file(temp.arena, vertex_shader_path);
	struct shader_file fragment_shader_code = read_file(temp.arena, fragment_shader_path);

	if (vertex_shader_code.size <= 0 && fragment_shader_code.size <= 0) {
		LOG_ERROR("Failed to load files");
		arena_reset_scratch(temp);
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

	arena_reset_scratch(temp);

	return true;
}

bool vulkan_renderer_create_pipeline(VulkanContext *context, uint32_t store_index, uint32_t shader_index) {
	if (store_index >= MAX_PIPELINES) {
		LOG_ERROR("Vulkan: Pipeline index %d out of bounds", store_index);
		return false;
	}

	VulkanPipeline *pipeline = &context->pipeline_pool[store_index];
	if (pipeline->handle != NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to allocatepipeline at index %d, but index is already in use", store_index);
		assert(false);
		return false;
	}

	pipeline->shader_index = shader_index;
	VulkanShader *shader = &context->shader_pool[pipeline->shader_index];
	if (shader->vertex_shader == NULL || shader->fragment_shader == NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to allocatepipeline with shader at index %d, but index is not in use", pipeline->shader_index);
		assert(false);
		return false;
	}

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
		.dynamicStateCount = array_count(dynamic_states),
		.pDynamicStates = dynamic_states
	};

	VkPipelineVertexInputStateCreateInfo vis_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = shader->binding_count,
		.pVertexBindingDescriptions = shader->bindings,
		.vertexAttributeDescriptionCount = shader->attribute_count,
		.pVertexAttributeDescriptions = shader->attributes
	};

	VkPipelineInputAssemblyStateCreateInfo ias_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
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
		.polygonMode = VK_POLYGON_MODE_FILL,
		.lineWidth = 1.0f,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
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
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
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
		.stageCount = array_count(shader_stages),
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

	if (vkCreateGraphicsPipelines(context->device.logical, VK_NULL_HANDLE, 1, &gp_create_info, NULL, &pipeline->handle) != VK_SUCCESS) {
		LOG_ERROR("Vulkan: Failed to create pipeline");
		return false;
	}

	LOG_INFO("VkPipeline created");
	return true;
}

bool vulkan_renderer_bind_pipeline(VulkanContext *context, uint32_t retrieve_index) {
	if (retrieve_index >= MAX_PIPELINES) {
		LOG_ERROR("Vulkan: Pipeline index %d out of bounds", retrieve_index);
		return false;
	}

	VulkanPipeline *pipeline = &context->pipeline_pool[retrieve_index];
	if (pipeline->handle == NULL) {
		LOG_FATAL("Engine: Frontend renderer tried to bind pipeline at index %d, but index is not in use", retrieve_index);
		assert(false);
		return false;
	}
	vkCmdBindPipeline(context->command_buffers[context->current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);

	// VulkanShader *shader = &context->shader_pool[pipeline->shader_index];
	// vkCmdBindDescriptorSets(
	// 	context->command_buffers[context->current_frame],
	// 	VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline_layout,
	// 	0, 1, &shader->descriptor_sets[context->current_frame], 0, NULL);
	return true;
}

struct shader_file read_file(struct arena *arena, const char *path) {
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		LOG_ERROR("Failed to read file %s: %s", path, strerror(errno));
		return (struct shader_file){ 0 };
	}

	fseek(file, 0L, SEEK_END);
	uint32_t size = ftell(file);
	fseek(file, 0L, SEEK_SET);

	uint8_t *byte_content = arena_push_array_zero(arena, uint8_t, size);
	fread(byte_content, sizeof(*byte_content), size, file);

	fclose(file);

	return (struct shader_file){ .size = size, .content = byte_content };
}

bool reflect_shader_interface(VulkanContext *context, VulkanShader *shader, struct shader_file vertex_shader_code, struct shader_file fragment_shader_code) {
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

	ArenaTemp temp = arena_get_scratch(NULL);

	// ========== VERTEX INPUT ATTRIBUTES ==========
	uint32_t input_variable_count = 0;
	result = spvReflectEnumerateInputVariables(&vertex_module, &input_variable_count, NULL);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);

	SpvReflectInterfaceVariable **input_variables = arena_push_array(temp.arena, SpvReflectInterfaceVariable *, input_variable_count);
	result = spvReflectEnumerateInputVariables(&vertex_module, &input_variable_count, input_variables);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);

	shader->attribute_count = 0;
	shader->binding_count = 0;

	// Track unique bindings
	uint32_t bindings_used[MAX_BINDINGS] = { 0 };

	for (uint32_t index = 0; index < input_variable_count; ++index) {
		SpvReflectInterfaceVariable *var = input_variables[index];

		// Skip built-ins (gl_VertexIndex, etc.)
		if (var->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) {
			continue;
		}

		VkVertexInputAttributeDescription *attr = &shader->attributes[shader->attribute_count];
		attr->location = var->location;
		attr->binding = 0; // Assume single binding for now
		attr->format = (VkFormat)var->format;
		attr->offset = 0; // Will be calculated based on previous attributes

		shader->attribute_count++;
		bindings_used[0] = 1; // Mark binding 0 as used

		assert(shader->attribute_count < MAX_ATTRIBUTES);
	}

	// Create binding descriptions for used bindings
	uint32_t stride = 0;
	for (uint32_t index = 0; index < shader->attribute_count; ++index) {
		shader->attributes[index].offset = stride;

		// Calculate stride based on format
		VkFormat format = shader->attributes[index].format;
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
	assert(result == SPV_REFLECT_RESULT_SUCCESS);
	result = spvReflectEnumerateDescriptorSets(&fragment_module, &fs_set_count, NULL);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);

	SpvReflectDescriptorSet **vs_sets = arena_push_array(temp.arena, SpvReflectDescriptorSet *, vs_set_count);
	SpvReflectDescriptorSet **fs_sets = arena_push_array(temp.arena, SpvReflectDescriptorSet *, fs_set_count);

	result = spvReflectEnumerateDescriptorSets(&vertex_module, &vs_set_count, vs_sets);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);
	result = spvReflectEnumerateDescriptorSets(&fragment_module, &fs_set_count, fs_sets);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);

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

	// Create descriptor set layouts
	shader->set_count = unique_set_count;
	for (uint32_t index = 0; index < unique_set_count; ++index) {
		SetInfo *info = &merged_sets[index];

		if (vulkan_create_descriptor_set_layout(context, info->bindings, info->binding_count, &shader->layouts[index]) == false) {
			spvReflectDestroyShaderModule(&vertex_module);
			spvReflectDestroyShaderModule(&fragment_module);
			arena_reset_scratch(temp);
			return false;
		}
	}

	// ========== PUSH CONSTANTS ==========
	uint32_t vs_push_count = 0, fs_push_count = 0;
	result = spvReflectEnumeratePushConstantBlocks(&vertex_module, &vs_push_count, NULL);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);
	result = spvReflectEnumeratePushConstantBlocks(&fragment_module, &fs_push_count, NULL);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);

	shader->ps_count = 0;

	uint32_t push_constant_offset = shader->uniform_count;

	if (vs_push_count > 0) {
		SpvReflectBlockVariable **push_blocks = arena_push_array(temp.arena, SpvReflectBlockVariable *, vs_push_count);
		result = spvReflectEnumeratePushConstantBlocks(&vertex_module, &vs_push_count, push_blocks);
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

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
		SpvReflectBlockVariable **push_blocks = arena_push_array(temp.arena, SpvReflectBlockVariable *, fs_push_count);
		result = spvReflectEnumeratePushConstantBlocks(&fragment_module, &fs_push_count, push_blocks);
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

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

	// ========== CREATE PIPELINE LAYOUT ==========
	VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = shader->set_count,
		.pSetLayouts = shader->layouts,
		.pushConstantRangeCount = shader->ps_count,
		.pPushConstantRanges = shader->push_constant_ranges,
	};

	if (vkCreatePipelineLayout(context->device.logical, &pipeline_layout_info, NULL, &shader->pipeline_layout) != VK_SUCCESS) {
		LOG_ERROR("Failed to create pipeline layout");
		spvReflectDestroyShaderModule(&vertex_module);
		spvReflectDestroyShaderModule(&fragment_module);
		return false;
	}

	// Cleanup reflection modules
	spvReflectDestroyShaderModule(&vertex_module);
	spvReflectDestroyShaderModule(&fragment_module);

	arena_reset_scratch(temp);

	return true;
}

// int32_t ftovkf(ShaderAttributeFormat format) {
// 	switch (format) {
// 		case SHADER_ATTRIBUTE_FORMAT_FLOAT:
// 			return VK_FORMAT_R32_SFLOAT;
// 		case SHADER_ATTRIBUTE_FORMAT_FLOAT2:
// 			return VK_FORMAT_R32G32_SFLOAT;
// 		case SHADER_ATTRIBUTE_FORMAT_FLOAT3:
// 			return VK_FORMAT_R32G32B32_SFLOAT;
// 		case SHADER_ATTRIBUTE_FORMAT_FLOAT4:
// 			return VK_FORMAT_R32G32B32A32_SFLOAT;
//
// 		case SHADER_ATTRIBUTE_FORMAT_INT:
// 			return VK_FORMAT_R32_SINT;
// 		case SHADER_ATTRIBUTE_FORMAT_INT2:
// 			return VK_FORMAT_R32G32_SINT;
// 		case SHADER_ATTRIBUTE_FORMAT_INT3:
// 			return VK_FORMAT_R32G32B32_SINT;
// 		case SHADER_ATTRIBUTE_FORMAT_INT4:
// 			return VK_FORMAT_R32G32B32A32_SINT;
//
// 		case SHADER_ATTRIBUTE_FORMAT_UINT:
// 			return VK_FORMAT_R32_UINT;
// 		case SHADER_ATTRIBUTE_FORMAT_UINT2:
// 			return VK_FORMAT_R32G32_UINT;
// 		case SHADER_ATTRIBUTE_FORMAT_UINT3:
// 			return VK_FORMAT_R32G32B32_UINT;
// 		case SHADER_ATTRIBUTE_FORMAT_UINT4:
// 			return VK_FORMAT_R32G32B32A32_UINT;
// 		default: {
// 			LOG_ERROR("Unkown attribute format type provided!");
// 			return 0;
// 		} break;
// 	}
// }
//
// uint32_t ftobs(ShaderAttributeFormat format) {
// 	switch (format) {
// 		case SHADER_ATTRIBUTE_FORMAT_FLOAT:
// 			return sizeof(float);
// 		case SHADER_ATTRIBUTE_FORMAT_FLOAT2:
// 			return sizeof(float) * 2;
// 		case SHADER_ATTRIBUTE_FORMAT_FLOAT3:
// 			return sizeof(float) * 3;
// 		case SHADER_ATTRIBUTE_FORMAT_FLOAT4:
// 			return sizeof(float) * 4;
//
// 		case SHADER_ATTRIBUTE_FORMAT_INT:
// 			return sizeof(int32_t);
// 		case SHADER_ATTRIBUTE_FORMAT_INT2:
// 			return sizeof(int32_t) * 2;
// 		case SHADER_ATTRIBUTE_FORMAT_INT3:
// 			return sizeof(int32_t) * 3;
// 		case SHADER_ATTRIBUTE_FORMAT_INT4:
// 			return sizeof(int32_t) * 4;
//
// 		case SHADER_ATTRIBUTE_FORMAT_UINT:
// 			return sizeof(uint32_t);
// 		case SHADER_ATTRIBUTE_FORMAT_UINT2:
// 			return sizeof(uint32_t) * 2;
// 		case SHADER_ATTRIBUTE_FORMAT_UINT3:
// 			return sizeof(uint32_t) * 3;
// 		case SHADER_ATTRIBUTE_FORMAT_UINT4:
// 			return sizeof(uint32_t) * 4;
// 		default: {
// 			LOG_ERROR("Unkown attribute format type provided!");
// 			return 0;
// 		} break;
// 	}
// }
