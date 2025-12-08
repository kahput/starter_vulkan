#ifndef RENDERER_TYPES_H
#define RENDERER_TYPES_H

#include "common.h"
#include "core/identifiers.h"

#include <cglm/cglm.h>

typedef struct {
	mat4 view;
	mat4 projection;
} CameraUpload;

typedef struct {
	vec3 position;
	vec3 normal;
	vec2 uv;
	vec4 tangent;
} Vertex;

typedef struct texture_source {
	void *pixels;

	int32_t width, height, channels;
	char *path;
} TextureSource;

typedef struct material_source {
	TextureSource *base_color_texture;
	TextureSource *metallic_roughness_texture; // G = Roughness, B = Metallic
	TextureSource *normal_texture;
	TextureSource *occlusion_texture;
	TextureSource *emissive_texture;

	vec4 base_color_factor;
	float metallic_factor;
	float roughness_factor;
	vec3 emissive_factor;
} MaterialSource;

typedef struct material_paramters {
	vec4 base_color_factor;
	float metallic_factor;
	float roughness_factor;
	vec2 _pad0;
	vec4 emissive_factor;
} MaterialParameters;

typedef struct mesh_source {
	Vertex *vertices;
	uint32_t vertex_count;

	uint32_t *indices;
	uint32_t index_count;

	MaterialSource *material;
} MeshSource;

typedef struct scene_source {
	MeshSource *meshes;
	uint32_t mesh_count;

	MaterialSource *materials;
	uint32_t material_count;

	TextureSource *textures;
	uint32_t texture_count;
} SceneAsset;

typedef struct texture {
	uint32_t texture, sampler;
} Texture;

typedef struct material {
	uint32_t shader, pipeline;
} Material;

typedef struct material_instance {
	Material material;
	uint32_t resource_set, parameter_uniform_buffer;
} MaterialInstance;

typedef struct mesh {
	uint32_t vertex_buffer, index_buffer;
	uint32_t vertex_count, index_count;

	uint32_t material;
} Mesh;

typedef enum buffer_type {
	BUFFER_TYPE_VERTEX,
	BUFFER_TYPE_INDEX,
	BUFFER_TYPE_UNIFORM,
} BufferType;

typedef enum shader_stage {
	SHADER_STAGE_VERTEX,
	SHADER_STAGE_FRAGMENT,
} ShaderStage;

typedef enum {
	SHADER_UNIFORM_FREQUENCY_PER_FRAME,
	SHADER_UNIFORM_FREQUENCY_PER_MATERIAL,
	SHADER_UNIFORM_FREQUENCY_PER_OBJECT,
} ShaderUniformFrequency;

typedef enum {
	SHADER_UNIFORM_UNDEFINED,
	SHADER_UNIFORM_TYPE_BUFFER,
	SHADER_UNIFORM_TYPE_CONSTANTS,
	SHADER_UNIFORM_TYPE_COMBINED_IMAGE_SAMPLER
} ShaderUniformType;

typedef enum shader_attribute_format {
	SHADER_ATTRIBUTE_FORMAT_FLOAT,
	SHADER_ATTRIBUTE_FORMAT_FLOAT2,
	SHADER_ATTRIBUTE_FORMAT_FLOAT3,
	SHADER_ATTRIBUTE_FORMAT_FLOAT4,

	SHADER_ATTRIBUTE_FORMAT_INT,
	SHADER_ATTRIBUTE_FORMAT_INT2,
	SHADER_ATTRIBUTE_FORMAT_INT3,
	SHADER_ATTRIBUTE_FORMAT_INT4,

	SHADER_ATTRIBUTE_FORMAT_UINT,
	SHADER_ATTRIBUTE_FORMAT_UINT2,
	SHADER_ATTRIBUTE_FORMAT_UINT3,
	SHADER_ATTRIBUTE_FORMAT_UINT4,

	SHADER_ATTRIBUTE_FORMAT_MAT3,
	SHADER_ATTRIBUTE_FORMAT_MAT4,

	SHADER_ATTRIBUTE_FORMAT_LAST
} ShaderAttributeFormat;

typedef struct shader_attribute {
	const char *name;
	ShaderAttributeFormat format;
	uint8_t binding;
} ShaderAttribute;

typedef struct uniform_binding {
	char name[64];
	ShaderUniformType type;
	ShaderStage stage;
	uint32_t size, count;

	uint32_t set, binding;
} ShaderUniform;

typedef enum cull_mode {
	CULL_MODE_NONE = 0,
	CULL_MODE_FRONT = 1,
	CULL_MODE_BACK = 2,
	CULL_MODE_FRONT_AND_BACK = 3
} PipelineCullMode;

typedef enum front_face {
	FRONT_FACE_COUNTER_CLOCKWISE = 0, // Default in most engines
	FRONT_FACE_CLOCKWISE = 1 // Default in Vulkan coordinate system
} PipelineFrontFace;

typedef enum polygon_mode {
	POLYGON_MODE_FILL = 0,
	POLYGON_MODE_LINE = 1, // Wireframe
	POLYGON_MODE_POINT = 2,
} PipelinePolygonMode;

typedef enum compare_op {
	COMPARE_OP_NEVER = 0,
	COMPARE_OP_LESS = 1,
	COMPARE_OP_EQUAL = 2,
	COMPARE_OP_LESS_OR_EQUAL = 3,
	COMPARE_OP_GREATER = 4,
	COMPARE_OP_NOT_EQUAL = 5,
	COMPARE_OP_GREATER_OR_EQUAL = 6,
	COMPARE_OP_ALWAYS = 7
} PipelineCompareOp;

typedef struct pipeline_desc {
	uint32_t shader_index;

	ShaderAttribute *attributes;
	uint32_t attribute_count;

	PipelineCullMode cull_mode;
	PipelineFrontFace front_face;
	PipelinePolygonMode polygon_mode;
	float line_width;

	bool depth_test_enable;
	bool depth_write_enable;
	PipelineCompareOp depth_compare_op;

	bool blend_enable;

	bool topology_line_list;
} PipelineDesc;

#define DEFAULT_PIPELINE(index)                     \
	(PipelineDesc) {                                \
		.shader_index = index,                      \
		.attributes = NULL,                         \
		.attribute_count = 0,                       \
		.cull_mode = CULL_MODE_BACK,                \
		.front_face = FRONT_FACE_COUNTER_CLOCKWISE, \
		.polygon_mode = POLYGON_MODE_FILL,          \
		.line_width = 1.0f,                         \
		.depth_test_enable = true,                  \
		.depth_write_enable = true,                 \
		.depth_compare_op = COMPARE_OP_LESS,        \
		.blend_enable = false,                      \
		.topology_line_list = false                 \
	}

#endif
