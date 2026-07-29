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
	  .permutations = { { 0 } },
	  .permutation_count = 1,
	},
	[SHADER_SPATIAL] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/base.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/phong.fragment.spv"),
	  },
	  .permutations = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_8,
		  .cull_mode = CULL_MODE_BACK,
		},
	  },
	  .permutation_count = 1,
	},
	[SHADER_GRASS] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/grass.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/grass.fragment.spv"),
	  },
	  .permutations = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_8,
		},
	  },
	  .permutation_count = 1,
	},
	[SHADER_SKYBOX] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/skybox.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/skybox.fragment.spv"),
	  },
	  .permutations = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_8,
		},
	  },
	  .permutation_count = 1,
	},
	[SHADER_LINE3D] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/line.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/flat.fragment.spv"),
	  },
	  .permutations = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_8,
		},
	  },
	  .permutation_count = 1,
	},
	[SHADER_TRANSPARENT] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/base.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/energyfield.fragment.spv"),
	  },
	  .permutations = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_8,
		  .cull_mode = CULL_MODE_NONE,
		},
	  },
	  .permutation_count = 1,
	},
	[SHADER_QUAD2D] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/batch2d.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/quad.fragment.spv"),
	  },
	  .permutations = {
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
	  .permutation_count = 1,
	},
	[SHADER_LINE2D] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/line2d.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/flat.fragment.spv"),
	  },
	  .permutations = {
		{
		  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_8,
		  .cull_mode = CULL_MODE_BACK,
		  .disable_depth_test = true,
		  .disable_depth_write = true,
		},
	  },
	  .permutation_count = 1,
	},
	[SHADER_COMPOSITE] = {
	  .filepaths = {
		[SHADER_STAGE_VERTEX] = str_comp("assets/shaders/vertex/bin/fullscreen_quad.vertex.spv"),
		[SHADER_STAGE_FRAGMENT] = str_comp("assets/shaders/fragment/bin/composite.fragment.spv"),
	  },
	  .permutations = {
		{
		  .color_attachments = { PIXEL_FORMAT_BGRA8_UNORM },
		  .color_attachment_count = 1,
		  .sample_count = SAMPLE_COUNT_1,
		  .cull_mode = CULL_MODE_BACK,
		  .disable_depth_test = true,
		  .disable_depth_write = true,
		},
	  },
	  .permutation_count = 1,
	},
};

String8 texture_slot_to_string[TEXTURE_SLOT_COUNT] = {
	[TEXTURE_SLOT_ALBEDO] = str_comp("albedo"),
	[TEXTURE_SLOT_METAL_ROUGHNESS] = str_comp("metal_roughness"),
	[TEXTURE_SLOT_NORMAL] = str_comp("normal"),
	[TEXTURE_SLOT_OCCLUSION] = str_comp("occlusion"),
	[TEXTURE_SLOT_EMISSIVE] = str_comp("emissive"),
};

String8 icon_to_filepath[ICON_MAX] = {
	[ICON_PLAY] = str_comp("assets/icons/PNG/White/1x/forward.png"),
	[ICON_PAUSE] = str_comp("assets/icons/PNG/White/1x/pause.png"),
	[ICON_STOP] = str_comp("assets/icons/PNG/White/1x/stop.png")
};

String8 icon_to_string[ICON_MAX] = {
	ENUM_STRING_TABLE_ENTRY(ICON, PLAY),
	ENUM_STRING_TABLE_ENTRY(ICON, PAUSE),
	ENUM_STRING_TABLE_ENTRY(ICON, STOP),
};

String8 meshid_to_string[MESH_MAX] = {
	ENUM_STRING_TABLE_ENTRY(MESH, HERO_MALE),
	ENUM_STRING_TABLE_ENTRY(MESH, GDBOT),
	ENUM_STRING_TABLE_ENTRY(MESH, MAGE),
	ENUM_STRING_TABLE_ENTRY(MESH, BARREL),
	ENUM_STRING_TABLE_ENTRY(MESH, ROOM),
	ENUM_STRING_TABLE_ENTRY(MESH, TEST_LEVEL),
	ENUM_STRING_TABLE_ENTRY(MESH, ROOM_LARGE),
	ENUM_STRING_TABLE_ENTRY(MESH, TERRAIN_FLAT),
	ENUM_STRING_TABLE_ENTRY(MESH, TERRAIN_HEIGHTMAP),
	ENUM_STRING_TABLE_ENTRY(MESH, GRASS_BILLBOARD),
	ENUM_STRING_TABLE_ENTRY(MESH, CYLINDER),
	ENUM_STRING_TABLE_ENTRY(MESH, SPHERE),
	ENUM_STRING_TABLE_ENTRY(MESH, GIZMOS_ARROW),
};

String8 meshid_to_metadata[MESH_MAX] = {
	[MESH_HERO_MALE] = str_comp("assets/models/hero_male.glb"),
	[MESH_GDBOT] = str_comp("assets/models/gdbot.glb"),
	[MESH_MAGE] = str_comp("assets/models/mage.glb"),
	[MESH_BARREL] = str_comp("assets/models/barrel.glb"),
	[MESH_ROOM] = str_comp("assets/models/room.glb"),
	[MESH_ROOM_LARGE] = str_comp("assets/models/room-large.glb"),
	[MESH_TEST_LEVEL] = str_comp("assets/models/test_level.glb"),
	[MESH_GRASS_BILLBOARD] = str_comp("assets/models/grass.glb"),
};
