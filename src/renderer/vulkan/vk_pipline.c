#include "renderer/vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"

#include <errno.h>
#include "common.h"
#include <string.h>
#include <vulkan/vulkan_core.h>

struct shader_file {
	uint32_t size;
	uint8_t *content;
};

static inline int32_t vaf_to_vulkan_format(VertexAttributeFormat format);
static inline uint32_t vaf_to_byte_size(VertexAttributeFormat format);

struct shader_file read_file(struct arena *arena, const char *path);
bool create_shader_module(VulkanState *vk_state, VkShaderModule *module, struct shader_file code);

bool vk_create_graphics_pipline(VulkanState *vk_state) {
	ArenaTemp temp = arena_get_scratch(NULL);
	struct shader_file vertex_shader_code = read_file(temp.arena, "./assets/shaders/vs_default.spv");
	struct shader_file fragment_shader_code = read_file(temp.arena, "./assets/shaders/fs_default.spv");

	if (vertex_shader_code.size <= 0 && fragment_shader_code.size <= 0) {
		LOG_ERROR("Failed to load files");
		arena_reset_scratch(temp);
		return false;
	}

	VkShaderModule vertex_shader;
	create_shader_module(vk_state, &vertex_shader, vertex_shader_code);

	VkShaderModule fragment_shader;
	create_shader_module(vk_state, &fragment_shader, fragment_shader_code);

	arena_reset_scratch(temp);

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

	VertexAttribute attributes[] = {
		{ .name = "in_position", FORMAT_FLOAT3 },
		{ .name = "in_uv", FORMAT_FLOAT2 }
	};

	VkVertexInputBindingDescription binding_description = { 0 };
	VkVertexInputAttributeDescription attribute_descriptions[array_count(attributes)] = { 0 };

	uint32_t byte_offset = 0;
	for (uint32_t i = 0; i < array_count(attributes); ++i) {
		VertexAttribute attribute = attributes[i];
		attribute_descriptions[i] = (VkVertexInputAttributeDescription){
			.binding = 1,
			.location = i,
			.format = vaf_to_vulkan_format(attribute.format),
			.offset = byte_offset
		};

		byte_offset += vaf_to_byte_size(attribute.format);
	}

	binding_description = (VkVertexInputBindingDescription){
		.binding = 1,
		.stride = byte_offset,
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
	};

	VkPipelineVertexInputStateCreateInfo vis_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding_description,
		.vertexAttributeDescriptionCount = array_count(attributes),
		.pVertexAttributeDescriptions = attribute_descriptions
	};

	VkPipelineInputAssemblyStateCreateInfo ias_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE
	};

	VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = vk_state->swapchain.extent.width,
		.height = vk_state->swapchain.extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	VkRect2D scissor = {
		.offset = { 0.0f, 0.0f },
		.extent = vk_state->swapchain.extent
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

	VkPipelineLayoutCreateInfo pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &vk_state->descriptor_set_layout,
		.pushConstantRangeCount = 0,
	};

	if (vkCreatePipelineLayout(vk_state->device.logical, &pl_create_info, NULL, &vk_state->pipeline_layout) != VK_SUCCESS) {
		LOG_ERROR("Failed to create graphcis pipeline layout");
		vkDestroyShaderModule(vk_state->device.logical, vertex_shader, NULL);
		vkDestroyShaderModule(vk_state->device.logical, fragment_shader, NULL);
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

	VkGraphicsPipelineCreateInfo gp_create_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
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
		.layout = vk_state->pipeline_layout,
		.renderPass = vk_state->render_pass,
		.subpass = 0,
	};

	if (vkCreateGraphicsPipelines(vk_state->device.logical, VK_NULL_HANDLE, 1, &gp_create_info, NULL, &vk_state->graphics_pipeline) != VK_SUCCESS) {
		LOG_ERROR("Failed to create graphics pipeline");
		vkDestroyShaderModule(vk_state->device.logical, vertex_shader, NULL);
		vkDestroyShaderModule(vk_state->device.logical, fragment_shader, NULL);
		return false;
	}

	vkDestroyShaderModule(vk_state->device.logical, vertex_shader, NULL);
	vkDestroyShaderModule(vk_state->device.logical, fragment_shader, NULL);

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

bool create_shader_module(VulkanState *vk_state, VkShaderModule *module, struct shader_file code) {
	VkShaderModuleCreateInfo sm_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size,
		.pCode = (uint32_t *)code.content,
	};

	if (vkCreateShaderModule(vk_state->device.logical, &sm_create_info, NULL, module) != VK_SUCCESS) {
		LOG_ERROR("Failed to create shader module");
		return false;
	}

	return true;
}

int32_t vaf_to_vulkan_format(VertexAttributeFormat format) {
	switch (format) {
		case FORMAT_FLOAT:
			return VK_FORMAT_R32_SFLOAT;
		case FORMAT_FLOAT2:
			return VK_FORMAT_R32G32_SFLOAT;
		case FORMAT_FLOAT3:
			return VK_FORMAT_R32G32B32_SFLOAT;
		case FORMAT_FLOAT4:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
		default: {
			LOG_ERROR("Unkown attribute format type provided!");
			return 0;
		} break;
	}
}

uint32_t vaf_to_byte_size(VertexAttributeFormat format) {
	switch (format) {
		case FORMAT_FLOAT:
			return sizeof(float);
		case FORMAT_FLOAT2:
			return sizeof(float) * 2;
		case FORMAT_FLOAT3:
			return sizeof(float) * 3;
		case FORMAT_FLOAT4:
			return sizeof(float) * 4;
		default: {
			LOG_ERROR("Unkown attribute format type provided!");
			return 0;
		} break;
	}
}
