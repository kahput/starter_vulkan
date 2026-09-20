#include "assets_generated.h"

ShaderMetadata res_shaderid_to_metadata[RES_SHADER_MAX] = {
    [RES_SHADER_SHADOW] = {
      .name = scomp("Shadow"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = scomp("assets/shaders/generated/v_Shadow.spv"),
          [SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/generated/f_Shadow.spv"),
      },
      .pipelines = {
          [PIPELINE_SHADOW_DEFAULT] = {
              .cull_mode = CULL_MODE_NONE,
              .polygon_mode = POLYGON_MODE_FILL,
              .disable_depth_test = false,
              .disable_depth_write = false,
              .blend_enable = false,
              .color_op = BLEND_OP_ADD,
              .alpha_op = BLEND_OP_ADD,
              .src_color_factor = BLEND_FACTOR_ONE,
              .dst_color_factor = BLEND_FACTOR_ZERO,
              .src_alpha_factor = BLEND_FACTOR_ONE,
              .dst_alpha_factor = BLEND_FACTOR_ZERO,
          },
      },
      .pipeline_count = 1,
    },
    [RES_SHADER_SPATIAL] = {
      .name = scomp("Spatial"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = scomp("assets/shaders/generated/v_Spatial.spv"),
          [SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/generated/f_Spatial.spv"),
      },
      .pipelines = {
          [PIPELINE_SPATIAL_DEFAULT] = {
              .cull_mode = CULL_MODE_BACK,
              .polygon_mode = POLYGON_MODE_FILL,
              .disable_depth_test = false,
              .disable_depth_write = false,
              .blend_enable = false,
              .color_op = BLEND_OP_ADD,
              .alpha_op = BLEND_OP_ADD,
              .src_color_factor = BLEND_FACTOR_ONE,
              .dst_color_factor = BLEND_FACTOR_ZERO,
              .src_alpha_factor = BLEND_FACTOR_ONE,
              .dst_alpha_factor = BLEND_FACTOR_ZERO,
          },
          [PIPELINE_SPATIAL_BLENDED] = {
              .cull_mode = CULL_MODE_FRONT,
              .polygon_mode = POLYGON_MODE_FILL,
              .disable_depth_test = true,
              .disable_depth_write = false,
              .blend_enable = true,
              .color_op = BLEND_OP_SUB,
              .alpha_op = BLEND_OP_SUB,
              .src_color_factor = BLEND_FACTOR_ONE,
              .dst_color_factor = BLEND_FACTOR_ONE,
              .src_alpha_factor = BLEND_FACTOR_ONE,
              .dst_alpha_factor = BLEND_FACTOR_ONE,
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
              .polygon_mode = POLYGON_MODE_FILL,
              .disable_depth_test = false,
              .disable_depth_write = false,
              .blend_enable = true,
              .color_op = BLEND_OP_ADD,
              .alpha_op = BLEND_OP_SUB,
              .src_color_factor = BLEND_FACTOR_ONE,
              .dst_color_factor = BLEND_FACTOR_ONE,
              .src_alpha_factor = BLEND_FACTOR_ONE,
              .dst_alpha_factor = BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
          },
      },
      .pipeline_count = 1,
    },
    [RES_SHADER_GRASS] = {
      .name = scomp("Grass"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = scomp("assets/shaders/generated/v_Grass.spv"),
          [SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/generated/f_Grass.spv"),
      },
      .pipelines = {
          [PIPELINE_GRASS_DEFAULT] = {
              .cull_mode = CULL_MODE_NONE,
              .polygon_mode = POLYGON_MODE_FILL,
              .disable_depth_test = false,
              .disable_depth_write = false,
              .blend_enable = false,
              .color_op = BLEND_OP_ADD,
              .alpha_op = BLEND_OP_ADD,
              .src_color_factor = BLEND_FACTOR_ONE,
              .dst_color_factor = BLEND_FACTOR_ZERO,
              .src_alpha_factor = BLEND_FACTOR_ONE,
              .dst_alpha_factor = BLEND_FACTOR_ZERO,
          },
      },
      .pipeline_count = 1,
    },
    [RES_SHADER_SKYBOX] = {
      .name = scomp("Skybox"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = scomp("assets/shaders/generated/v_Skybox.spv"),
          [SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/generated/f_Skybox.spv"),
      },
      .pipelines = {
          [PIPELINE_SKYBOX_DEFAULT] = {
              .cull_mode = CULL_MODE_NONE,
              .polygon_mode = POLYGON_MODE_FILL,
              .disable_depth_test = false,
              .disable_depth_write = false,
              .blend_enable = false,
              .color_op = BLEND_OP_ADD,
              .alpha_op = BLEND_OP_ADD,
              .src_color_factor = BLEND_FACTOR_ONE,
              .dst_color_factor = BLEND_FACTOR_ZERO,
              .src_alpha_factor = BLEND_FACTOR_ONE,
              .dst_alpha_factor = BLEND_FACTOR_ZERO,
          },
      },
      .pipeline_count = 1,
    },
    [RES_SHADER_LINE3D] = {
      .name = scomp("Line3D"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = scomp("assets/shaders/generated/v_Line3D.spv"),
          [SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/generated/f_Line3D.spv"),
      },
      .pipelines = {
          [PIPELINE_LINE3D_DEFAULT] = {
              .cull_mode = CULL_MODE_NONE,
              .polygon_mode = POLYGON_MODE_FILL,
              .disable_depth_test = false,
              .disable_depth_write = false,
              .blend_enable = false,
              .color_op = BLEND_OP_ADD,
              .alpha_op = BLEND_OP_ADD,
              .src_color_factor = BLEND_FACTOR_ONE,
              .dst_color_factor = BLEND_FACTOR_ZERO,
              .src_alpha_factor = BLEND_FACTOR_ONE,
              .dst_alpha_factor = BLEND_FACTOR_ZERO,
          },
      },
      .pipeline_count = 1,
    },
    [RES_SHADER_TESTCOMPUTE] = {
      .name = scomp("TestCompute"),
      .filepaths = {
          [SHADER_STAGE_COMPUTE] = scomp("assets/shaders/generated/c_TestCompute.spv"),
      },
    },
    [RES_SHADER_SKINNING] = {
      .name = scomp("Skinning"),
      .filepaths = {
          [SHADER_STAGE_COMPUTE] = scomp("assets/shaders/generated/c_Skinning.spv"),
      },
    },
    [RES_SHADER_COMPOSITE] = {
      .name = scomp("Composite"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = scomp("assets/shaders/generated/v_Composite.spv"),
          [SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/generated/f_Composite.spv"),
      },
      .pipelines = {
          [PIPELINE_COMPOSITE_DEFAULT] = {
              .cull_mode = CULL_MODE_BACK,
              .polygon_mode = POLYGON_MODE_FILL,
              .disable_depth_test = true,
              .disable_depth_write = true,
              .blend_enable = false,
              .color_op = BLEND_OP_ADD,
              .alpha_op = BLEND_OP_ADD,
              .src_color_factor = BLEND_FACTOR_ONE,
              .dst_color_factor = BLEND_FACTOR_ZERO,
              .src_alpha_factor = BLEND_FACTOR_ONE,
              .dst_alpha_factor = BLEND_FACTOR_ZERO,
          },
      },
      .pipeline_count = 1,
    },
    [RES_SHADER_QUAD2D] = {
      .name = scomp("Quad2D"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = scomp("assets/shaders/generated/v_Quad2D.spv"),
          [SHADER_STAGE_FRAGMENT] = scomp("assets/shaders/generated/f_Quad2D.spv"),
      },
      .pipelines = {
          [PIPELINE_QUAD2D_DEFAULT] = {
              .cull_mode = CULL_MODE_NONE,
              .polygon_mode = POLYGON_MODE_FILL,
              .disable_depth_test = true,
              .disable_depth_write = true,
              .blend_enable = true,
              .color_op = BLEND_OP_ADD,
              .alpha_op = BLEND_OP_ADD,
              .src_color_factor = BLEND_FACTOR_SRC_ALPHA,
              .dst_color_factor = BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
              .src_alpha_factor = BLEND_FACTOR_SRC_ALPHA,
              .dst_alpha_factor = BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
          },
      },
      .pipeline_count = 1,
    },
};

