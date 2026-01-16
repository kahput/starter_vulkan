#include "assets/asset_types.h"
#include "renderer/r_internal.h"
#include "vk_internal.h"
#include "renderer/backend/vulkan_api.h"

#include <assert.h>
#include <spirv_reflect/spirv_reflect.h>

#include "common.h"
#include "core/debug.h"
#include "core/logger.h"
#include "core/astring.h"
#include "core/arena.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

bool create_shader_variant(VulkanContext *context, VulkanShader *shader, VulkanPass *pass, PipelineDesc description, VulkanPipeline *variant);
bool reflect_shader_interface(Arena *arena, VulkanContext *context, VulkanShader *shader, VulkanGlobalResource *global, ShaderConfig *config, ShaderReflection *out_reflection);

bool vulkan_renderer_shader_create(
	Arena *arena, VulkanContext *context,
	uint32_t store_index, uint32_t compatible_global, uint32_t pass_interface,
	ShaderConfig *config, PipelineDesc description, ShaderReflection *out_reflection) {
	VulkanShader *shader = NULL;
	VULKAN_GET_OR_RETURN(shader, context->shader_pool, store_index, MAX_SHADERS, false);

	VulkanPass *pass = NULL;
	VULKAN_GET_OR_RETURN(pass, context->pass_pool, pass_interface, MAX_RENDER_PASSES, true);

	VulkanGlobalResource *global = NULL;
	VULKAN_GET_OR_RETURN(global, context->global_resources, compatible_global, MAX_GLOBAL_RESOURCES, true);

	if (config->vertex_code == NULL || config->vertex_code_size == 0 || config->fragment_code == NULL || config->fragment_code_size == 0) {
		LOG_ERROR("Vulkan: Invalid shader code passed to vulkan_renderer_shader_craete");
		return false;
	}

	VkShaderModuleCreateInfo vsm_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = config->vertex_code_size,
		.pCode = (uint32_t *)config->vertex_code,
	};

	if (vkCreateShaderModule(context->device.logical, &vsm_create_info, NULL, &shader->vertex_shader) != VK_SUCCESS) {
		LOG_ERROR("Failed to create vertex shader module");
		return false;
	}

	VkShaderModuleCreateInfo fsm_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = config->fragment_code_size,
		.pCode = (uint32_t *)config->fragment_code,
	};

	if (vkCreateShaderModule(context->device.logical, &fsm_create_info, NULL, &shader->fragment_shader) != VK_SUCCESS) {
		LOG_ERROR("Failed to create fragment shader module");
		return false;
	}

	reflect_shader_interface(arena, context, shader, global, config, out_reflection);
	shader->bind_variant = description.polygon_mode == POLYGON_MODE_FILL ? 0 : 1;

	description.polygon_mode = POLYGON_MODE_FILL;
	create_shader_variant(context, shader, pass, description, &shader->variants[0]);

	description.polygon_mode = POLYGON_MODE_LINE;
	create_shader_variant(context, shader, pass, description, &shader->variants[1]);
	shader->state = VULKAN_RESOURCE_STATE_INITIALIZED;

	return true;
}

bool vulkan_renderer_shader_destroy(VulkanContext *context, uint32_t retrieve_index) {
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

	vkDestroyDescriptorSetLayout(context->device.logical, shader->group_layout, NULL);
	vkDestroyPipelineLayout(context->device.logical, shader->pipeline_layout, NULL);

	for (uint32_t index = 0; index < shader->variant_count; ++index) {
		VulkanPipeline *pipeline = &shader->variants[index];

		if (pipeline->handle) {
			vkDestroyPipeline(context->device.logical, pipeline->handle, NULL);
			pipeline->handle = NULL;
		}
	}

	*shader = (VulkanShader){ 0 };
	return true;
}

struct vertex_input_state {
	VkVertexInputAttributeDescription attributes[MAX_INPUT_ATTRIBUTES];
	VkVertexInputBindingDescription bindings[MAX_INPUT_BINDINGS];

	uint32_t binding_count, attribute_count;
};

static bool override_attributes(struct vertex_input_state *state, ShaderAttribute *attributes, uint32_t attribute_count);
static void resolve_pass_formats(VulkanContext *context, VulkanPass *pass, VkFormat *out_color_formats, VkFormat *out_depth_format);
bool create_shader_variant(VulkanContext *context, VulkanShader *shader, VulkanPass *pass, PipelineDesc description, VulkanPipeline *variant) {
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
		.rasterizationSamples = pass->enable_msaa ? context->device.sample_count : VK_SAMPLE_COUNT_1_BIT,
		.minSampleShading = 1.0f,
	};

	VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		.blendEnable = VK_FALSE,
	};

	VkPipelineColorBlendAttachmentState color_blend_attachments[4];
	for (uint32_t index = 0; index < countof(color_blend_attachments); ++index) {
		color_blend_attachments[index] = (VkPipelineColorBlendAttachmentState){
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
			.blendEnable = VK_FALSE,
		};
	}

	VkPipelineColorBlendStateCreateInfo cbs_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.attachmentCount = pass->color_attachment_count,
		.pAttachments = color_blend_attachments,
	};

	// NOTE: This should be handled by the pass, and not the pipeline description?
	VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = pass->depth_attachment.state == VULKAN_RESOURCE_STATE_INITIALIZED,
		.depthWriteEnable = pass->depth_attachment.state == VULKAN_RESOURCE_STATE_INITIALIZED,
		.depthCompareOp = (VkCompareOp)description.depth_compare_op,
		.depthBoundsTestEnable = VK_FALSE,
		.minDepthBounds = 0.0f,
		.maxDepthBounds = 1.0f
	};

	VkFormat color_formats[4];
	VkFormat depth_format;
	resolve_pass_formats(context, pass, color_formats, &depth_format);

	VkPipelineRenderingCreateInfo r_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = pass->color_attachment_count,
		.pColorAttachmentFormats = color_formats,
		.depthAttachmentFormat = depth_format
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

bool vulkan_renderer_shader_bind(VulkanContext *context, uint32_t shader_index) {
	if (shader_index >= MAX_SHADERS) {
		LOG_ERROR("Vulkan: shader index %d out of bounds, aborting vulkan_renderer_shader_bind", shader_index);
		return false;
	}

	VulkanShader *shader = &context->shader_pool[shader_index];
	VulkanPipeline *pipeline = &shader->variants[shader->bind_variant];
	if (pipeline->handle == NULL) {
		ASSERT_FORMAT(false, "Engine: Frontend renderer tried to bind shader at index %d, but index is not in use", shader_index);
		return false;
	}
	context->bound_shader = shader;

	vkCmdBindPipeline(context->command_buffers[context->current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);
	return true;
}

void vulkan_renderer_shader_global_state_wireframe_set(VulkanContext *context, bool active) {
	for (uint32_t index = 0; index < MAX_SHADERS; ++index) {
		VulkanShader *shader = &context->shader_pool[index];
		if (shader == NULL)
			continue;

		shader->bind_variant = active ? 1 : shader->bind_variant == 1 ? 0
																	  : shader->bind_variant;
	}
}

static uint32_t count_shader_members(SpvReflectBlockVariable *var) {
	if (var->member_count == 0)
		return 1;

	uint32_t count = 0;
	for (uint32_t i = 0; i < var->member_count; ++i) {
		count += count_shader_members(&var->members[i]);
	}
	return count;
}

static void fill_shader_members(Arena *arena, SpvReflectBlockVariable *var, String prefix, ShaderMember **cursor) {
	char full_name[128];
	if (prefix.length > 0)
		snprintf(full_name, sizeof(full_name), "%s.%s", prefix.data, var->name);
	else
		snprintf(full_name, sizeof(full_name), "%s", var->name);

	if (var->member_count == 0) {
		// This is a leaf node
		ShaderMember *m = *cursor;
		m->name = string_duplicate(arena, string_wrap_cstring(full_name));
		m->offset = var->absolute_offset;
		m->size = var->size;

		(*cursor)++; // Advance pointer
	} else {
		// Recurse
		for (uint32_t i = 0; i < var->member_count; ++i) {
			fill_shader_members(arena, &var->members[i], S(full_name), cursor);
		}
	}
}

static ShaderBuffer *parse_buffer_layout(Arena *arena, SpvReflectBlockVariable *block) {
	ShaderBuffer *buffer = arena_push_struct_zero(arena, ShaderBuffer);
	buffer->name = string_duplicate(arena, string_wrap_cstring(block->name));
	buffer->size = block->size;

	buffer->member_count = count_shader_members(block);
	buffer->members = arena_push_array_zero(arena, ShaderMember, buffer->member_count);

	ShaderMember *cursor = buffer->members;
	for (uint32_t i = 0; i < block->member_count; ++i) {
		fill_shader_members(arena, &block->members[i], S(""), &cursor);
	}

	return buffer;
}

bool reflect_shader_interface(Arena *arena, VulkanContext *context, VulkanShader *shader, VulkanGlobalResource *global, ShaderConfig *config, ShaderReflection *out_reflection) {
	SpvReflectShaderModule vertex_module, fragment_module;
	SpvReflectResult result;

	result = spvReflectCreateShaderModule(config->vertex_code_size, config->vertex_code, &vertex_module);
	if (result != SPV_REFLECT_RESULT_SUCCESS) {
		LOG_ERROR("Failed to reflect vertex shader");
		return false;
	}

	result = spvReflectCreateShaderModule(config->fragment_code_size, config->fragment_code, &fragment_module);
	if (result != SPV_REFLECT_RESULT_SUCCESS) {
		LOG_ERROR("Failed to reflect fragment shader");
		spvReflectDestroyShaderModule(&vertex_module);
		return false;
	}

	ArenaTemp scratch = arena_scratch(NULL);

	uint32_t input_variable_count = 0;
	result = spvReflectEnumerateInputVariables(&vertex_module, &input_variable_count, NULL);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

	SpvReflectInterfaceVariable **input_variables = arena_push_array_zero(scratch.arena, SpvReflectInterfaceVariable *, input_variable_count);
	result = spvReflectEnumerateInputVariables(&vertex_module, &input_variable_count, input_variables);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

	shader->attribute_count = 0;
	shader->binding_count = 0;

	uint32_t max_attributes = context->device.properties.limits.maxVertexInputAttributes;
	uint32_t *indices = arena_push_array_zero(scratch.arena, uint32_t, max_attributes);
	memset(indices, INT32_MAX, sizeof(uint32_t) * max_attributes);

	for (uint32_t index = 0; index < input_variable_count; ++index) {
		SpvReflectInterfaceVariable *var = input_variables[index];
		if (var->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)
			continue;

		VkVertexInputAttributeDescription *attr = &shader->attributes[index];
		indices[var->location] = index;
		attr->location = var->location;
		attr->binding = 0;
		attr->format = (VkFormat)var->format;
		attr->offset = 0;

		shader->attribute_count++;
		ASSERT(shader->attribute_count < MAX_INPUT_ATTRIBUTES);
	}

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

		// TODO: Add more types
		VkFormat format = attr->format;
		if (format == VK_FORMAT_R32_SFLOAT)
			stride += 4;
		else if (format == VK_FORMAT_R32G32_SFLOAT)
			stride += 8;
		else if (format == VK_FORMAT_R32G32B32_SFLOAT)
			stride += 12;
		else if (format == VK_FORMAT_R32G32B32A32_SFLOAT)
			stride += 16;
	}

	if (shader->attribute_count) {
		shader->bindings[0].binding = 0;
		shader->bindings[0].stride = stride;
		shader->bindings[0]
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		shader->binding_count = 1;
	}

	uint32_t vs_set_count = 0, fs_set_count = 0;
	result = spvReflectEnumerateDescriptorSets(&vertex_module, &vs_set_count, NULL);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);
	result = spvReflectEnumerateDescriptorSets(&fragment_module, &fs_set_count, NULL);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

	SpvReflectDescriptorSet **vs_sets = arena_push_array_zero(scratch.arena, SpvReflectDescriptorSet *, vs_set_count);
	SpvReflectDescriptorSet **fs_sets = arena_push_array_zero(scratch.arena, SpvReflectDescriptorSet *, fs_set_count);

	result = spvReflectEnumerateDescriptorSets(&vertex_module, &vs_set_count, vs_sets);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);
	result = spvReflectEnumerateDescriptorSets(&fragment_module, &fs_set_count, fs_sets);
	ASSERT(result == SPV_REFLECT_RESULT_SUCCESS);

	typedef struct {
		uint32_t binding_count;
		VkDescriptorSetLayoutBinding vk_binding[32];
		SpvReflectDescriptorBinding *spv_binding[32];
	} SetInfo;

	SetInfo merged_sets[MAX_SETS] = { 0 };
	uint32_t max_set_index = 0;

	for (uint32_t set_index = 0; set_index < vs_set_count; ++set_index) {
		SpvReflectDescriptorSet *spv_set = vs_sets[set_index];
		if (spv_set->set > MAX_SETS)
			continue;
		SetInfo *set = &merged_sets[spv_set->set];
		if (spv_set->set > max_set_index)
			max_set_index = spv_set->set;

		for (uint32_t binding_index = 0; binding_index < spv_set->binding_count; ++binding_index) {
			SpvReflectDescriptorBinding *spv_binding = spv_set->bindings[binding_index];

			set->spv_binding[set->binding_count] = spv_binding;
			VkDescriptorSetLayoutBinding *vk = &set->vk_binding[set->binding_count++];

			vk->binding = spv_binding->binding;
			vk->descriptorType = (VkDescriptorType)spv_binding->descriptor_type;
			vk->descriptorCount = spv_binding->count;
			vk->stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			vk->pImmutableSamplers = NULL;
		}
	}

	for (uint32_t set_index = 0; set_index < fs_set_count; ++set_index) {
		SpvReflectDescriptorSet *reflect_set = fs_sets[set_index];
		if (reflect_set->set > MAX_SETS)
			continue;

		SetInfo *set = &merged_sets[reflect_set->set];
		if (reflect_set->set > max_set_index)
			max_set_index = reflect_set->set;

		for (uint32_t binding_index = 0; binding_index < reflect_set->binding_count; ++binding_index) {
			SpvReflectDescriptorBinding *reflect_binding = reflect_set->bindings[binding_index];

			int32_t found_index = -1;
			for (uint32_t existing = 0; existing < set->binding_count; ++existing) {
				if (set->vk_binding[existing].binding == reflect_binding->binding) {
					found_index = existing;
					break;
				}
			}
			if (found_index != -1)
				set->vk_binding[found_index].stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
			else {
				set->spv_binding[set->binding_count] = reflect_binding;
				VkDescriptorSetLayoutBinding *vk = &set->vk_binding[set->binding_count++];

				vk->binding = reflect_binding->binding;
				vk->descriptorType = (VkDescriptorType)reflect_binding->descriptor_type;
				vk->descriptorCount = reflect_binding->count;
				vk->stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
				vk->pImmutableSamplers = NULL;
			}
		}
	}

	out_reflection->binding_count = 0;
	for (uint32_t i = 0; i <= max_set_index; ++i)
		out_reflection->binding_count += merged_sets[i].binding_count;

	out_reflection->bindings = arena_push_array_zero(arena, ShaderBinding, out_reflection->binding_count);

	for (uint32_t set_index = 0, index = 0; set_index <= max_set_index; ++set_index) {
		SetInfo *set = &merged_sets[set_index];
		for (uint32_t binding_index = 0; binding_index < set->binding_count; ++binding_index, ++index) {
			SpvReflectDescriptorBinding *spv = set->spv_binding[binding_index];
			VkDescriptorSetLayoutBinding *vk = &set->vk_binding[binding_index];

			ShaderBinding *dst = &out_reflection->bindings[index];

			dst->name = string_duplicate(arena, string_wrap_cstring(spv->name));

			dst->frequency = spv->set;
			dst->binding = spv->binding;
			dst->count = spv->count;
			if ((vk->stageFlags & VK_SHADER_STAGE_VERTEX_BIT) == VK_SHADER_STAGE_VERTEX_BIT)
				dst->stage |= SHADER_STAGE_VERTEX;
			if ((vk->stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT) == VK_SHADER_STAGE_FRAGMENT_BIT)
				dst->stage |= SHADER_STAGE_FRAGMENT;

			VkDescriptorType type = vk->descriptorType;
			dst->buffer_layout = NULL;

			if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
				dst->type = SHADER_BINDING_UNIFORM_BUFFER;
				dst->buffer_layout = parse_buffer_layout(arena, &spv->block);
			} else if (type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
				dst->type = SHADER_BINDING_STORAGE_BUFFER;
				dst->buffer_layout = parse_buffer_layout(arena, &spv->block);
			} else if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
				dst->type = SHADER_BINDING_TEXTURE_2D;
			}
		}
	}

	SetInfo *global_reflect_info = &merged_sets[SHADER_UNIFORM_FREQUENCY_PER_FRAME];
	for (uint32_t binding_index = 0; binding_index < global->binding_count; ++binding_index) {
		VkDescriptorSetLayoutBinding *global_binding = &global->bindings[binding_index];

		bool valid = false;
		for (uint32_t reflect_index = 0; reflect_index < global_reflect_info->binding_count; ++reflect_index) {
			VkDescriptorSetLayoutBinding *reflect_binding = &global_reflect_info->vk_binding[reflect_index];
			VkDescriptorType type =
				global_binding->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
				? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
				: global_binding->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
				? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
				: global_binding->descriptorType;

			if (reflect_binding->binding == global_binding->binding && reflect_binding->descriptorType == type)
				valid = true;
		}

		if (valid == false) {
			LOG_WARN("Vulkan: shader is not compatible with passed global resource");
			ASSERT(false);
		}
	}

	SetInfo *material_reflect_info = &merged_sets[SHADER_UNIFORM_FREQUENCY_PER_MATERIAL];
	for (uint32_t index = 0; index < material_reflect_info->binding_count; ++index) {
		VkDescriptorSetLayoutBinding *vk_binding = &material_reflect_info->vk_binding[index];
		SpvReflectDescriptorBinding *binding = material_reflect_info->spv_binding[index];

		if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
			ASSERT(shader->instance_size == 0);

			vk_binding->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			shader->instance_size = binding->block.size;
			shader->group_ubo_binding = binding->binding;
		}
	}

	if (vulkan_descriptor_layout_create(context, material_reflect_info->vk_binding, material_reflect_info->binding_count, &shader->group_layout) == false) {
		spvReflectDestroyShaderModule(&vertex_module);
		spvReflectDestroyShaderModule(&fragment_module);
		arena_release_scratch(scratch);
		return false;
	}

	spvReflectDestroyShaderModule(&vertex_module);
	spvReflectDestroyShaderModule(&fragment_module);
	arena_release_scratch(scratch);

	VkDescriptorSetLayout layouts[] = {
		[SHADER_UNIFORM_FREQUENCY_PER_FRAME] = global->set_layout,
		[SHADER_UNIFORM_FREQUENCY_PER_MATERIAL] = shader->group_layout
	};

	VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 2,
		.pSetLayouts = layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &context->global_range,
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

void resolve_pass_formats(VulkanContext *context, VulkanPass *pass, VkFormat *out_color_formats, VkFormat *out_depth_format) {
	ASSERT(pass->color_attachment_count <= 4);

	for (uint32_t color_index = 0; color_index < pass->color_attachment_count; ++color_index) {
		if (pass->color_attachments[color_index].present)
			out_color_formats[color_index] = context->swapchain.format.format;
		else
			out_color_formats[color_index] = context->image_pool[pass->color_attachments[color_index].image_index].info.format;
	}

	if (pass->depth_attachment.state == VULKAN_RESOURCE_STATE_INITIALIZED) {
		*out_depth_format = context->image_pool[pass->depth_attachment.image_index].info.format;
	} else
		*out_depth_format = VK_FORMAT_UNDEFINED;
}
