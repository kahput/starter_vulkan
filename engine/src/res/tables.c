#include "tables.h"

String8 shaderid_to_string[SHADER_MAX] = {
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

ShaderMetadata shaderid_to_metadata[SHADER_MAX] = {
	[SHADER_TEST_COMPUTE] = {
	  .name = scomp("Test Compute"),
	  .filepaths[SHADER_STAGE_COMPUTE] = scomp("assets/shaders/compute/bin/test.compute.spv"),
	},
	[SHADER_SKINNING_COMPUTE] = {
	  .name = scomp("Skinning Compute"),
	  .filepaths[SHADER_STAGE_COMPUTE] = scomp("assets/shaders/compute/bin/skinning.compute.spv"),
	},
	[SHADER_SHADOW] = {
	  .name = scomp("Shadow"),
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = scomp("assets/shaders/vertex/bin/shadow.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/fragment/bin/blank.fragment.spv"),
	  },
	  .pipelines = { { 0 } },
	  .pipeline_count = 1,
	},
	[SHADER_SPATIAL] = {
	  .name = scomp("Spatial"),
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = scomp("assets/shaders/vertex/bin/base.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/fragment/bin/phong.fragment.spv"),
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
	  .name = scomp("Grass"),
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = scomp("assets/shaders/vertex/bin/grass.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/fragment/bin/grass.fragment.spv"),
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
	  .name = scomp("Skybox"),
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = scomp("assets/shaders/vertex/bin/skybox.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/fragment/bin/skybox.fragment.spv"),
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
	  .name = scomp("Line 3D"),
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = scomp("assets/shaders/vertex/bin/line.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/fragment/bin/flat.fragment.spv"),
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
	  .name = scomp("Transparent"),
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = scomp("assets/shaders/vertex/bin/base.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/fragment/bin/energyfield.fragment.spv"),
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
	  .name = scomp("Quad 2D"),
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = scomp("assets/shaders/vertex/bin/batch2d.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/fragment/bin/quad.fragment.spv"),
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
	  .name = scomp("Line 2D"),
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = scomp("assets/shaders/vertex/bin/line2d.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/fragment/bin/flat.fragment.spv"),
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
	  .name = scomp("Composite"),
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = scomp("assets/shaders/vertex/bin/fullscreen_quad.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/fragment/bin/composite.fragment.spv"),
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
