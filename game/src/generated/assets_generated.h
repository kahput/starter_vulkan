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
    RES_SHADER_SHADOW,
#define PIPELINE_SHADOW_DEFAULT 0
    RES_SHADER_SPATIAL,
#define PIPELINE_SPATIAL_DEFAULT 0
#define PIPELINE_SPATIAL_BLENDED 1
    RES_SHADER_TRANSPARENT,
#define PIPELINE_TRANSPARENT_DEFAULT 0
    RES_SHADER_GRASS,
#define PIPELINE_GRASS_DEFAULT 0
    RES_SHADER_SKYBOX,
#define PIPELINE_SKYBOX_DEFAULT 0
    RES_SHADER_LINE3D,
#define PIPELINE_LINE3D_DEFAULT 0
    RES_SHADER_TESTCOMPUTE,
#define PIPELINE_TESTCOMPUTE_DEFAULT 0
    RES_SHADER_SKINNING,
#define PIPELINE_SKINNING_DEFAULT 0
    RES_SHADER_COMPOSITE,
#define PIPELINE_COMPOSITE_DEFAULT 0
    RES_SHADER_QUAD2D,
#define PIPELINE_QUAD2D_DEFAULT 0

    RES_SHADER_MAX
} RES_ShaderID;

extern ShaderMetadata res_shaderid_to_metadata[RES_SHADER_MAX];
