#include "assets_generated.h"

ShaderMetadata shaderid_to_metadata[RES_SHADER_MAX] = {
    [RES_SHADER_SPATIAL] = {
      .name = scomp("Spatial"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = scomp("assets/shaders/generated/v_Spatial.spv"),
          [SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/generated/f_Spatial.spv"),
      },
      .pipelines = {
          [PIPELINE_SPATIAL_DEFAULT] = {
              .cull_mode = CULL_MODE_NONE,
              .blend_enable = false,
              .color_op = BLEND_OP_ADD,
              .alpha_op = BLEND_OP_ADD,
              .src_color_factor = BLEND_FACTOR_ZERO,
              .dst_color_factor = BLEND_FACTOR_ZERO,
              .src_alpha_factor = BLEND_FACTOR_ZERO,
              .dst_alpha_factor = BLEND_FACTOR_ZERO,
          },
          [PIPELINE_SPATIAL_BLENDED] = {
              .cull_mode = CULL_MODE_NONE,
              .blend_enable = true,
              .color_op = BLEND_OP_SUB,
              .alpha_op = BLEND_OP_ADD,
              .src_color_factor = BLEND_FACTOR_ONE,
              .dst_color_factor = BLEND_FACTOR_ONE,
              .src_alpha_factor = BLEND_FACTOR_ZERO,
              .dst_alpha_factor = BLEND_FACTOR_ZERO,
          },
      },
      .pipeline_count = 2,
    },
    [RES_SHADER_TRANSPARENT] = {
      .name = scomp("Transparent"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = scomp("assets/shaders/generated/v_Transparent.spv"),
          [SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/generated/f_Transparent.spv"),
      },
      .pipelines = {
          [PIPELINE_TRANSPARENT_DEFAULT] = {
              .cull_mode = CULL_MODE_NONE,
              .blend_enable = true,
              .color_op = BLEND_OP_SUB,
              .alpha_op = BLEND_OP_SUB,
              .src_color_factor = BLEND_FACTOR_ONE,
              .dst_color_factor = BLEND_FACTOR_ONE,
              .src_alpha_factor = BLEND_FACTOR_ONE,
              .dst_alpha_factor = BLEND_FACTOR_ONE,
          },
      },
      .pipeline_count = 1,
    },
};

