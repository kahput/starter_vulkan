#pragma once
#include "core/strings.h"
#include "gfx/gfx_types.h"

typedef struct {
	String8 name;
	String8 filepaths[SHADER_STAGE_MAX];
	PipelineOptions pipelines[8];
	uint32_t pipeline_count;
} ShaderMetadata;

typedef enum {
	SHADER_TEST_COMPUTE,
	SHADER_SKINNING_COMPUTE,
	SHADER_SHADOW,
	SHADER_SPATIAL,
	SHADER_GRASS,
	SHADER_SKYBOX,
	SHADER_LINE3D,
	SHADER_TRANSPARENT,
	SHADER_QUAD2D,
	SHADER_LINE2D,
	SHADER_COMPOSITE,

	SHADER_MAX,
} ShaderID;

extern String8 shaderid_to_string[SHADER_MAX];
extern ShaderMetadata shaderid_to_metadata[SHADER_MAX];
