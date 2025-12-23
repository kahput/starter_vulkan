#pragma once

#include "common.h"
#include "core/astring.h"
#include "core/identifiers.h"

#include <cglm/cglm.h>

typedef enum buffer_type {
	BUFFER_TYPE_VERTEX,
	BUFFER_TYPE_INDEX,
	BUFFER_TYPE_UNIFORM,
} BufferType;

typedef enum shader_attribute_format {
	SHADER_ATTRIBUTE_TYPE_UNDEFINED,

	SHADER_ATTRIBUTE_TYPE_FLOAT64,
	SHADER_ATTRIBUTE_TYPE_INT64,
	SHADER_ATTRIBUTE_TYPE_UINT64,

	SHADER_ATTRIBUTE_TYPE_FLOAT32,
	SHADER_ATTRIBUTE_TYPE_INT32,
	SHADER_ATTRIBUTE_TYPE_UINT32,

	SHADER_ATTRIBUTE_TYPE_INT16,
	SHADER_ATTRIBUTE_TYPE_UINT16,

	SHADER_ATTRIBUTE_TYPE_INT8,
	SHADER_ATTRIBUTE_TYPE_UINT8,

	SHADER_ATTRIBUTE_TYPE_LAST
} ShaderAttributeType;

typedef struct {
	ShaderAttributeType type;
	uint32_t count;
} ShaderAttributeFormat;

typedef struct shader_attribute {
	String name;
	ShaderAttributeFormat format;
	uint32_t binding;
	size_t offset;
} ShaderAttribute;

typedef enum shader_stage {
	SHADER_STAGE_VERTEX = 1 << 0,
	SHADER_STAGE_FRAGMENT = 1 << 1,
} ShaderStageFlagBits;
typedef uint32_t ShaderStageFlag;

typedef enum {
	SHADER_UNIFORM_FREQUENCY_PER_FRAME,
	SHADER_UNIFORM_FREQUENCY_PER_MATERIAL,
	SHADER_UNIFORM_FREQUENCY_PER_OBJECT,
} ShaderUniformFrequency;

typedef enum {
	SHADER_BINDING_UNDEFINED,
	SHADER_BINDING_UNIFORM_BUFFER,
	SHADER_BINDING_STORAGE_BUFFER,
	SHADER_BINDING_COMBINED_IMAGE_SAMPLER
} ShaderBindingType;

typedef struct shader_member {
	String name;
	size_t offset, size;
} ShaderMember;

typedef struct shader_buffer {
	String name;
	size_t size;

	ShaderMember *members;
	uint32_t member_count;
} ShaderBuffer;

typedef struct shader_binding {
	String name;
	ShaderBindingType type;
	ShaderStageFlag stage;
	ShaderUniformFrequency frequency;
	uint32_t binding, count;

	ShaderBuffer *buffer_layout;
} ShaderBinding;

typedef struct shader_constant {
	String name;
	ShaderStageFlag stage;

	ShaderBuffer *buffer;
} ShaderPushConstant;

typedef struct {
	ShaderBinding *bindings;
	uint32_t binding_count;

	ShaderPushConstant *push_constants;
	uint32_t push_constant_count;
} ShaderReflection;

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
	ShaderAttribute *override_attributes;
	uint32_t override_count;

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

typedef enum sampler_filter {
	FILTER_NEAREST = 0,
	FILTER_LINEAR = 1
} SamplerFilter;

typedef enum sampler_address_mode {
	SAMPLER_ADDRESS_MODE_REPEAT = 0,
	SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT = 1,
	SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE = 2,
	SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER = 3
} SamplerAddressMode;

typedef struct sampler_desc {
	SamplerFilter min_filter;
	SamplerFilter mag_filter;

	SamplerFilter mipmap_filter;

	SamplerAddressMode address_mode_u;
	SamplerAddressMode address_mode_v;
	SamplerAddressMode address_mode_w;

	bool anisotropy_enable;
} SamplerDesc;

// TODO: Move these
#define DEFAULT_PIPELINE()                          \
	(PipelineDesc) {                                \
		.override_attributes = NULL,                \
		.override_count = 0,                        \
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

#define LINEAR_SAMPLER                                 \
	(SamplerDesc) {                                    \
		.min_filter = FILTER_LINEAR,                   \
		.mag_filter = FILTER_LINEAR,                   \
		.address_mode_u = SAMPLER_ADDRESS_MODE_REPEAT, \
		.address_mode_v = SAMPLER_ADDRESS_MODE_REPEAT, \
		.address_mode_w = SAMPLER_ADDRESS_MODE_REPEAT, \
		.anisotropy_enable = true                      \
	}

#define NEAREST_SAMPLER                                       \
	(SamplerDesc) {                                           \
		.min_filter = FILTER_NEAREST,                         \
		.mag_filter = FILTER_NEAREST,                         \
		.address_mode_u = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, \
		.address_mode_v = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, \
		.address_mode_w = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, \
		.anisotropy_enable = false                            \
	}

typedef enum {
	MATERIAL_INSTANCE_STATE_DIRTY = 1 << 0,
} MaterialStateFlagBits;
typedef uint32_t MaterialStateFlag;

typedef struct {
	Handle handle;
	ShaderReflection reflection;

	void *default_ubo_data;
	size_t ubo_size;

	Handle default_textures[16];
	uint32_t instance_count;
} Shader;

typedef struct {
	Handle shader;
	uint32_t override_resource_id, override_ubo_id;

	MaterialStateFlag flags;
} Material;

typedef struct {
	mat4 view;
	mat4 projection;
	vec3 camera_position;
	float _pad0;
} FrameData;

typedef struct material_paramters {
	vec4 base_color_factor;
	float metallic_factor;
	float roughness_factor;
	vec2 _pad0;
	vec4 emissive_factor;
} MaterialParameters;

typedef struct texture {
	uint32_t handle, sampler;
} Texture;

typedef struct mesh {
	uint32_t vertex_buffer, index_buffer;
	uint32_t vertex_count, index_count;

	Handle material;
} Mesh;
