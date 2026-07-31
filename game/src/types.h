#pragma once

#include <common.h>
#include <core/strings.h>
#include <core/geom_types.h>

#include <gfx/gfx_types.h>
#include <utils/anim.h>

typedef struct {
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

typedef enum {
	TEXTURE_SLOT_ALBEDO,
	TEXTURE_SLOT_METAL_ROUGHNESS,
	TEXTURE_SLOT_NORMAL,
	TEXTURE_SLOT_OCCLUSION,
	TEXTURE_SLOT_EMISSIVE,

	TEXTURE_SLOT_COUNT,
} TextureSlot;

typedef enum {
	ICON_PLAY,
	ICON_PAUSE,
	ICON_STOP,

	ICON_MAX,
} IconID;

typedef struct {
	float2 position, uv;
	float4 radii;
	float2 size;
	uint32_t fill_color, border_color;
	float border_width;
	uint32_t imageid;
} QuadVertex2D;

typedef struct {
	float4 a, b; // xyz + thickness
	uint32_t color;
	float3 _pad0;
} LineVertex3D;

typedef struct {
	float3 position;
	float _pad0;
	float3 normal;
	float _pad1;
	float2 uv;
	float4 tangent;
} Vertex3D;

typedef struct {
	uint4 bone_ids;
	float4 weights;
} SkinningVertex3D;

typedef struct {
	Image2D textures[TEXTURE_SLOT_COUNT];

	float4 tint, emissive;
	float2 metallic_roughness;
} Material;

typedef struct {
	uint32_t vertex_offset, index_offset;
	uint32_t vertex_count, index_count;
	uint32_t material_id;

	AABB3 bounds;
} MeshPart;

typedef struct {
	// CPU
	Vertex3D *vertices;
	SkinningVertex3D *skinning;
	uint32_t *indices;

	// GPU
	GFX_Buffer *buffer;
	uint64_t buffer_vertex_byte_offset;
	uint64_t buffer_index_byte_offset;
	uint64_t buffer_skinning_data_byte_offset;

	Material *materials;
	uint32_t material_count;

	// METADATA
	MeshPart *parts;
	uint32_t part_count;
	Skeleton skeleton;

	uint64_t total_vertex_count;
	uint64_t total_index_count;

	AABB3 bounds;
} Mesh;

typedef enum {
	MESH_HERO_MALE,
	MESH_GDBOT,
	MESH_MAGE,
	MESH_BARREL,
	MESH_ROOM,
	MESH_TEST_LEVEL,

	MESH_ROOM_LARGE,
	MESH_TERRAIN_FLAT,
	MESH_TERRAIN_HEIGHTMAP,
	MESH_GRASS_BILLBOARD,

	MESH_CYLINDER,
	MESH_SPHERE,
	MESH_GIZMOS_ARROW,

	MESH_MAX,
} MeshID;
