#include "tables.h"

String8 shader_to_string[SHADER_MAX] = {
	ENUM_STRING_TABLE_ENTRY(SHADER, TEST_COMPUTE),
	ENUM_STRING_TABLE_ENTRY(SHADER, SKINNING_COMPUTE),
	ENUM_STRING_TABLE_ENTRY(SHADER, SHADOW),
	ENUM_STRING_TABLE_ENTRY(SHADER, SPATIAL),
	ENUM_STRING_TABLE_ENTRY(SHADER, GRASS),
	ENUM_STRING_TABLE_ENTRY(SHADER, SKYBOX),
	ENUM_STRING_TABLE_ENTRY(SHADER, LINE3D),
	ENUM_STRING_TABLE_ENTRY(SHADER, TRANSPARENT),
	ENUM_STRING_TABLE_ENTRY(SHADER, QUAD2D),
	ENUM_STRING_TABLE_ENTRY(SHADER, LINE2D),
	ENUM_STRING_TABLE_ENTRY(SHADER, COMPOSITE),
};

ShaderMetadata shader_to_metadata[SHADER_MAX] = {
	[SHADER_TEST_COMPUTE] = {
	  .filepaths[SHADER_STAGE_COMPUTE] = str_comp("assets/shaders/compute/bin/test.compute.spv") },
	[SHADER_SKINNING_COMPUTE] = { .filepaths[SHADER_STAGE_COMPUTE] = str_comp("assets/shaders/compute/bin/skinning.compute.spv") },
	[SHADER_SHADOW] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/shadow.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/blank.fragment.spv"),
	  },
	  .pipelines = { { 0 } },
	  .pipeline_count = 1,
	},
	[SHADER_SPATIAL] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/base.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/phong.fragment.spv"),
	  },
	  .pipelines = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_8,
		  .cull_mode = CULL_MODE_BACK,
		},
	  },
	  .pipeline_count = 1,
	},
	[SHADER_GRASS] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/grass.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/grass.fragment.spv"),
	  },
	  .pipelines = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_8,
		},
	  },
	  .pipeline_count = 1,
	},
	[SHADER_SKYBOX] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/skybox.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/skybox.fragment.spv"),
	  },
	  .pipelines = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_8,
		},
	  },
	  .pipeline_count = 1,
	},
	[SHADER_LINE3D] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/line.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/flat.fragment.spv"),
	  },
	  .pipelines = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_8,
		},
	  },
	  .pipeline_count = 1,
	},
	[SHADER_TRANSPARENT] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/base.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/energyfield.fragment.spv"),
	  },
	  .pipelines = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_8,
		  .cull_mode = CULL_MODE_NONE,
		},
	  },
	  .pipeline_count = 1,
	},
	[SHADER_QUAD2D] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/batch2d.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/quad.fragment.spv"),
	  },
	  .pipelines = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_UNORM },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_1,
		  .cull_mode = CULL_MODE_BACK,
		  .disable_depth_test = true,
		  .disable_depth_write = true,

		  .enable_blend = true,
		  .src_color_factor = BLEND_FACTOR_SRC_ALPHA,
		  .dst_color_factor = BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		  .src_alpha_factor = BLEND_FACTOR_ONE,
		  .dst_alpha_factor = BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		},
	  },
	  .pipeline_count = 1,
	},
	[SHADER_LINE2D] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/line2d.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/flat.fragment.spv"),
	  },
	  .pipelines = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_8,
		  .cull_mode = CULL_MODE_BACK,
		  .disable_depth_test = true,
		  .disable_depth_write = true,
		},
	  },
	  .pipeline_count = 1,
	},
	[SHADER_COMPOSITE] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/fullscreen_quad.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/composite.fragment.spv"),
	  },
	  .pipelines = {
		{
		  .color_attachments = { PIXEL_FORMAT_BGRA8_UNORM },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_1,
		  .cull_mode = CULL_MODE_BACK,
		  .disable_depth_test = true,
		  .disable_depth_write = true,
		},
	  },
	  .pipeline_count = 1,
	},
};
