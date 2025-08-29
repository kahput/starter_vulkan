#include "vk_renderer.h"

#include "core/arena.h"
#include "core/logger.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

struct shader_file {
	uint32_t size;
	uint8_t *content;
};

struct shader_file read_file(struct arena *arena, const char *path);
bool create_shader_module(VKRenderer *renderer, VkShaderModule *module, struct shader_file code);

bool vk_create_graphics_pipline(struct arena *arena, VKRenderer *renderer) {
	uint32_t offset = arena_size(arena);

	struct shader_file vertex_shader_code = read_file(arena, "./assets/shaders/vs_default.spv");
	struct shader_file fragment_shader_code = read_file(arena, "./assets/shaders/fs_default.spv");

	if (vertex_shader_code.size <= 0 && fragment_shader_code.size <= 0) {
		LOG_ERROR("Failed to load files");
		arena_set(arena, offset);
		return false;
	}

	VkShaderModule vertex_shader;
	create_shader_module(renderer, &vertex_shader, vertex_shader_code);

	VkShaderModule fragment_shader;
	create_shader_module(renderer, &fragment_shader, fragment_shader_code);

	arena_set(arena, offset);

	VkPipelineShaderStageCreateInfo vst_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = vertex_shader,
		.pName = "main"
	};
	VkPipelineShaderStageCreateInfo fst_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = fragment_shader,
		.pName = "main"
	};

	VkPipelineShaderStageCreateInfo shader_stages[] = { vst_create_info, fst_create_info };

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
	};

	VkPipelineInputAssemblyStateCreateInfo ias_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE
	};

	VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = renderer->swapchain_extent.width,
		.height = renderer->swapchain_extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	VkRect2D scissor = {
		.offset = { 0.0f, 0.0f },
		.extent = renderer->swapchain_extent
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
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
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
		.setLayoutCount = 0,
		.pushConstantRangeCount = 0
	};

	if (vkCreatePipelineLayout(renderer->logical_device, &pl_create_info, NULL, &renderer->pipeline_layout) != VK_SUCCESS) {
		LOG_ERROR("Failed to create graphcis pipeline layout");
		vkDestroyShaderModule(renderer->logical_device, vertex_shader, NULL);
		vkDestroyShaderModule(renderer->logical_device, fragment_shader, NULL);
		return false;
	}

	VkGraphicsPipelineCreateInfo gp_create_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = array_count(shader_stages),
		.pStages = shader_stages,
		.pVertexInputState = &vis_create_info,
		.pInputAssemblyState = &ias_create_info,
		.pViewportState = &vps_create_info,
		.pRasterizationState = &rs_create_info,
		.pMultisampleState = &mss_create_info,
		.pDepthStencilState = NULL,
		.pColorBlendState = &cbs_create_info,
		.pDynamicState = &ds_create_info,
		.layout = renderer->pipeline_layout,
		.renderPass = renderer->render_pass,
		.subpass = 0,
	};

	if (vkCreateGraphicsPipelines(renderer->logical_device, VK_NULL_HANDLE, 1, &gp_create_info, NULL, &renderer->graphics_pipeline) != VK_SUCCESS) {
		LOG_ERROR("Failed to create graphics pipeline");
		vkDestroyShaderModule(renderer->logical_device, vertex_shader, NULL);
		vkDestroyShaderModule(renderer->logical_device, fragment_shader, NULL);
		return false;
	}

	vkDestroyShaderModule(renderer->logical_device, vertex_shader, NULL);
	vkDestroyShaderModule(renderer->logical_device, fragment_shader, NULL);

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

bool create_shader_module(VKRenderer *renderer, VkShaderModule *module, struct shader_file code) {
	VkShaderModuleCreateInfo sm_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size,
		.pCode = (uint32_t *)code.content,
	};

	if (vkCreateShaderModule(renderer->logical_device, &sm_create_info, NULL, module) != VK_SUCCESS) {
		LOG_ERROR("Failed to create shader module");
		return false;
	}

	return true;
}
