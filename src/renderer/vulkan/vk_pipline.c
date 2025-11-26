#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"

#include "common.h"
#include <errno.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

struct shader_file {
	uint32_t size;
	uint8_t *content;
};

static inline int32_t ftovkf(ShaderAttributeFormat format);
static inline uint32_t ftobs(ShaderAttributeFormat format);

struct shader_file read_file(struct arena *arena, const char *path);
bool create_shader_module(VulkanContext *context, VkShaderModule *module, struct shader_file code);

bool vulkan_create_pipeline(VulkanContext *context, const char *vertex_shader_path, const char *fragment_shader_path, ShaderAttribute *attributes, uint32_t attribute_count, ShaderUniform *uniforms, uint32_t uniform_count) {
	ArenaTemp temp = arena_get_scratch(NULL);
	struct shader_file vertex_shader_code = read_file(temp.arena, vertex_shader_path);
	struct shader_file fragment_shader_code = read_file(temp.arena, fragment_shader_path);

	if (vertex_shader_code.size <= 0 && fragment_shader_code.size <= 0) {
		LOG_ERROR("Failed to load files");
		arena_reset_scratch(temp);
		return false;
	}

	VkShaderModule vertex_shader;
	create_shader_module(context, &vertex_shader, vertex_shader_code);

	VkShaderModule fragment_shader;
	create_shader_module(context, &fragment_shader, fragment_shader_code);

	VkPipelineShaderStageCreateInfo vss_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = vertex_shader,
		.pName = "main"
	};
	VkPipelineShaderStageCreateInfo fss_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = fragment_shader,
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

	VkVertexInputBindingDescription *binding_descriptions = arena_push_array_zero(temp.arena, VkVertexInputBindingDescription, attribute_count);
	VkVertexInputAttributeDescription *attribute_descriptions = arena_push_array(temp.arena, VkVertexInputAttributeDescription, attribute_count);

	uint32_t unique_bindings = 0;
	for (uint32_t attribute_index = 0; attribute_index < attribute_count; ++attribute_index) {
		ShaderAttribute attribute = attributes[attribute_index];
		uint32_t byte_offset = 0;
		for (uint32_t binding_index = 0; binding_index < attribute_count; ++binding_index) {
			VkVertexInputBindingDescription *binding_description = &binding_descriptions[binding_index];
			unique_bindings += binding_description->stride == 0;

			if (binding_description->binding == attribute.binding || binding_description->stride == 0) {
				binding_description->binding = attribute.binding;
				binding_description->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
				byte_offset = binding_description->stride;
				binding_description->stride += ftobs(attribute.format);
				break;
			}
		}

		VkVertexInputAttributeDescription attribute_description = {
			.binding = attribute.binding,
			.location = attribute_index,
			.format = ftovkf(attribute.format),
			.offset = byte_offset
		};

		attribute_descriptions[attribute_index] = attribute_description;
	}

	VkPipelineVertexInputStateCreateInfo vis_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = unique_bindings,
		.pVertexBindingDescriptions = binding_descriptions,
		.vertexAttributeDescriptionCount = attribute_count,
		.pVertexAttributeDescriptions = attribute_descriptions
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

	for (uint32_t uniform_index = 0; uniform_index < uniform_count; ++uniform_index) {
		ShaderUniform uniform = uniforms[uniform_index];
	}

	VkPipelineLayoutCreateInfo pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &context->descriptor_set_layout,
		.pushConstantRangeCount = 0,
	};

	if (vkCreatePipelineLayout(context->device.logical, &pl_create_info, NULL, &context->pipeline_layout) != VK_SUCCESS) {
		LOG_ERROR("Failed to create graphcis pipeline layout");
		vkDestroyShaderModule(context->device.logical, vertex_shader, NULL);
		vkDestroyShaderModule(context->device.logical, fragment_shader, NULL);
		return false;
	}

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
		.layout = context->pipeline_layout,
	};

	if (vkCreateGraphicsPipelines(context->device.logical, VK_NULL_HANDLE, 1, &gp_create_info, NULL, &context->graphics_pipeline) != VK_SUCCESS) {
		LOG_ERROR("Failed to create graphics pipeline");
		vkDestroyShaderModule(context->device.logical, vertex_shader, NULL);
		vkDestroyShaderModule(context->device.logical, fragment_shader, NULL);
		return false;
	}

	vkDestroyShaderModule(context->device.logical, vertex_shader, NULL);
	vkDestroyShaderModule(context->device.logical, fragment_shader, NULL);
	arena_reset_scratch(temp);

	LOG_INFO("Graphics pipeline created");
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

bool create_shader_module(VulkanContext *context, VkShaderModule *module, struct shader_file code) {
	VkShaderModuleCreateInfo sm_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size,
		.pCode = (uint32_t *)code.content,
	};

	if (vkCreateShaderModule(context->device.logical, &sm_create_info, NULL, module) != VK_SUCCESS) {
		LOG_ERROR("Failed to create shader module");
		return false;
	}

	return true;
}

int32_t ftovkf(ShaderAttributeFormat format) {
	switch (format) {
		case SHADER_ATTRIBUTE_FORMAT_FLOAT:
			return VK_FORMAT_R32_SFLOAT;
		case SHADER_ATTRIBUTE_FORMAT_FLOAT2:
			return VK_FORMAT_R32G32_SFLOAT;
		case SHADER_ATTRIBUTE_FORMAT_FLOAT3:
			return VK_FORMAT_R32G32B32_SFLOAT;
		case SHADER_ATTRIBUTE_FORMAT_FLOAT4:
			return VK_FORMAT_R32G32B32A32_SFLOAT;

		case SHADER_ATTRIBUTE_FORMAT_INT:
			return VK_FORMAT_R32_SINT;
		case SHADER_ATTRIBUTE_FORMAT_INT2:
			return VK_FORMAT_R32G32_SINT;
		case SHADER_ATTRIBUTE_FORMAT_INT3:
			return VK_FORMAT_R32G32B32_SINT;
		case SHADER_ATTRIBUTE_FORMAT_INT4:
			return VK_FORMAT_R32G32B32A32_SINT;

		case SHADER_ATTRIBUTE_FORMAT_UINT:
			return VK_FORMAT_R32_UINT;
		case SHADER_ATTRIBUTE_FORMAT_UINT2:
			return VK_FORMAT_R32G32_UINT;
		case SHADER_ATTRIBUTE_FORMAT_UINT3:
			return VK_FORMAT_R32G32B32_UINT;
		case SHADER_ATTRIBUTE_FORMAT_UINT4:
			return VK_FORMAT_R32G32B32A32_UINT;
		default: {
			LOG_ERROR("Unkown attribute format type provided!");
			return 0;
		} break;
	}
}

uint32_t ftobs(ShaderAttributeFormat format) {
	switch (format) {
		case SHADER_ATTRIBUTE_FORMAT_FLOAT:
			return sizeof(float);
		case SHADER_ATTRIBUTE_FORMAT_FLOAT2:
			return sizeof(float) * 2;
		case SHADER_ATTRIBUTE_FORMAT_FLOAT3:
			return sizeof(float) * 3;
		case SHADER_ATTRIBUTE_FORMAT_FLOAT4:
			return sizeof(float) * 4;

		case SHADER_ATTRIBUTE_FORMAT_INT:
			return sizeof(int32_t);
		case SHADER_ATTRIBUTE_FORMAT_INT2:
			return sizeof(int32_t) * 2;
		case SHADER_ATTRIBUTE_FORMAT_INT3:
			return sizeof(int32_t) * 3;
		case SHADER_ATTRIBUTE_FORMAT_INT4:
			return sizeof(int32_t) * 4;

		case SHADER_ATTRIBUTE_FORMAT_UINT:
			return sizeof(uint32_t);
		case SHADER_ATTRIBUTE_FORMAT_UINT2:
			return sizeof(uint32_t) * 2;
		case SHADER_ATTRIBUTE_FORMAT_UINT3:
			return sizeof(uint32_t) * 3;
		case SHADER_ATTRIBUTE_FORMAT_UINT4:
			return sizeof(uint32_t) * 4;
		default: {
			LOG_ERROR("Unkown attribute format type provided!");
			return 0;
		} break;
	}
}
