#include "assets.h"

RES_ShaderMeta res_shader_metadata[RES_SHADER_MAX] = {
    [RES_SHADER_UNLIT] = {
      .uuid = { 10699412431024026564ULL },
      .name = comp8("Unlit"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = comp8("/home/kahput/projects/starter_vulkan/assets/shaders/generated/v_Unlit.spv"),
          [SHADER_STAGE_FRAGMENT] = comp8("/home/kahput/projects/starter_vulkan/assets/shaders/generated/f_Unlit.spv"),
      },
      .pipelines = {
          [PIPELINE_UNLIT_DEFAULT] = {
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
          [PIPELINE_UNLIT_WIREFRAME] = {
              .cull_mode = CULL_MODE_NONE,
              .polygon_mode = POLYGON_MODE_LINE,
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
      .pipeline_count = 2,
    },
    [RES_SHADER_LINE3D] = {
      .uuid = { 9724632580759604653ULL },
      .name = comp8("Line3D"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = comp8("/home/kahput/projects/starter_vulkan/assets/shaders/generated/v_Line3D.spv"),
          [SHADER_STAGE_FRAGMENT] = comp8("/home/kahput/projects/starter_vulkan/assets/shaders/generated/f_Line3D.spv"),
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
      .uuid = { 17104535008024914169ULL },
      .name = comp8("TestCompute"),
      .filepaths = {
          [SHADER_STAGE_COMPUTE] = comp8("/home/kahput/projects/starter_vulkan/assets/shaders/generated/c_TestCompute.spv"),
      },
    },
    [RES_SHADER_SKINNING] = {
      .uuid = { 13527874114409681247ULL },
      .name = comp8("Skinning"),
      .filepaths = {
          [SHADER_STAGE_COMPUTE] = comp8("/home/kahput/projects/starter_vulkan/assets/shaders/generated/c_Skinning.spv"),
      },
    },
    [RES_SHADER_COMPOSITE] = {
      .uuid = { 3831973690472649551ULL },
      .name = comp8("Composite"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = comp8("/home/kahput/projects/starter_vulkan/assets/shaders/generated/v_Composite.spv"),
          [SHADER_STAGE_FRAGMENT] = comp8("/home/kahput/projects/starter_vulkan/assets/shaders/generated/f_Composite.spv"),
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
      .uuid = { 13230834648733991069ULL },
      .name = comp8("Quad2D"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = comp8("/home/kahput/projects/starter_vulkan/assets/shaders/generated/v_Quad2D.spv"),
          [SHADER_STAGE_FRAGMENT] = comp8("/home/kahput/projects/starter_vulkan/assets/shaders/generated/f_Quad2D.spv"),
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
              .src_alpha_factor = BLEND_FACTOR_ONE,
              .dst_alpha_factor = BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
          },
      },
      .pipeline_count = 1,
    },
    [RES_SHADER_QUAD2D_EXPERIMENT] = {
      .uuid = { 2374438767155098957ULL },
      .name = comp8("Quad2D_Experiment"),
      .filepaths = {
          [SHADER_STAGE_VERTEX] = comp8("/home/kahput/projects/starter_vulkan/assets/shaders/generated/v_Quad2D_Experiment.spv"),
          [SHADER_STAGE_FRAGMENT] = comp8("/home/kahput/projects/starter_vulkan/assets/shaders/generated/f_Quad2D_Experiment.spv"),
      },
      .pipelines = {
          [PIPELINE_QUAD2D_EXPERIMENT_DEFAULT] = {
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

