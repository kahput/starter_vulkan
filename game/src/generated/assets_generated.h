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
    RES_SHADER_SPATIAL,
#define PIPELINE_SPATIAL_DEFAULT 0
#define PIPELINE_SPATIAL_BLENDED 1
    RES_SHADER_TRANSPARENT,
#define PIPELINE_TRANSPARENT_DEFAULT 0

    RES_SHADER_MAX
} RES_ShaderID;

extern ShaderMetadata shaderid_to_metadata[RES_SHADER_MAX];
