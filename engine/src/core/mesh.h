#pragma once

#include "common.h"
#include "core/debug.h"
#include "core/geom_types.h"

// ============================================================================
// Data Formats
// ============================================================================

typedef enum {
	DATA_FORMAT_NONE = 0,

	DATA_FORMAT_FLOAT,
	DATA_FORMAT_FLOAT2,
	DATA_FORMAT_FLOAT3,
	DATA_FORMAT_FLOAT4,

	DATA_FORMAT_INT4,

	DATA_FORMAT_MAX,
} DataFormat;

extern uint32_t data_format_sizes[DATA_FORMAT_MAX];
INLINE uint32_t data_format_to_size(DataFormat format) {
	return ((uint32_t)format < DATA_FORMAT_MAX) ? data_format_sizes[format] : 0;
}

// ============================================================================
// Vertex Semantics & Layout
// ============================================================================

typedef enum {
	VERTEX_SEMANTIC_POSITION,
	VERTEX_SEMANTIC_NORMAL,
	VERTEX_SEMANTIC_TANGENT,
	VERTEX_SEMANTIC_UV0,
	VERTEX_SEMANTIC_UV1,
	VERTEX_SEMANTIC_COLOR0,
	VERTEX_SEMANTIC_JOINTS,
	VERTEX_SEMANTIC_WEIGHTS,

	VERTEX_SEMANTIC_CUSTOM0,
	VERTEX_SEMANTIC_CUSTOM1,
	VERTEX_SEMANTIC_CUSTOM2,
	VERTEX_SEMANTIC_CUSTOM3,

	VERTEX_SEMANTIC_MAX,
} VertexSemantic;

typedef struct {
	VertexSemantic semantic;
	DataFormat format;
} VertexAttribute;

#define VERTEX_LAYOUT_MAX_ATTRIBUTES 8
typedef struct {
	VertexAttribute attributes[VERTEX_LAYOUT_MAX_ATTRIBUTES];
	uint32_t attribute_count;
	uint32_t stride;
} VertexLayout;

#define vertex_layout(...)                        \
	vertex_layout_from_array(                     \
		(const VertexAttribute[]){ __VA_ARGS__ }, \
		sizeof((VertexAttribute[]){ __VA_ARGS__ }) / sizeof(VertexAttribute))

bool vertex_layout_push(VertexLayout *layout, VertexSemantic semantic, DataFormat format);
VertexLayout vertex_layout_from_array(const VertexAttribute *attributes, uint32_t attribute_count);

INLINE int32_t vertex_layout_find(const VertexLayout *layout, VertexSemantic semantic) {
	for (uint32_t index = 0; index < layout->attribute_count; ++index) {
		if (layout->attributes[index].semantic == semantic) {
			return (int32_t)index;
		}
	}
	return -1;
}

INLINE bool vertex_layout_has(const VertexLayout *layout, VertexSemantic semantic) {
	return vertex_layout_find(layout, semantic) >= 0;
}

// ============================================================================
// Index Formats
// ============================================================================

typedef enum {
	INDEX_FORMAT_NONE = 0,

	INDEX_FORMAT16,
	INDEX_FORMAT32,

	INDEX_FORMAT_MAX,
} IndexFormat;

INLINE uint32_t index_format_to_size(IndexFormat format) {
	switch (format) {
		case INDEX_FORMAT16:
			return 2;
		case INDEX_FORMAT32:
			return 4;
		default:
			return 0;
	}
}

// ============================================================================
// Mesh Structures & Functions
// ============================================================================

typedef struct {
	uint32_t vertex_offset, vertex_count;
	uint32_t index_offset, index_count;
	uint32_t material_slot;

	AABB3 bounds;
} MeshPart;

typedef struct {
	DataFormat stream_formats[VERTEX_SEMANTIC_MAX];
	void *streams[VERTEX_SEMANTIC_MAX];
	uint32_t total_vertex_count;

	IndexFormat index_format;
	void *indices;
	uint32_t total_index_count;

	MeshPart *parts;
	uint32_t part_count;

	AABB3 bounds;
} Mesh;

extern DataFormat vertex_semantic_default_format[VERTEX_SEMANTIC_MAX];
INLINE DataFormat mesh_stream_format(const Mesh *mesh, VertexSemantic semantic) {
	DataFormat override_fmt = mesh->stream_formats[semantic];
	return (override_fmt != DATA_FORMAT_NONE) ? override_fmt : vertex_semantic_default_format[semantic];
}

INLINE IndexFormat mesh_index_format(const Mesh *mesh) {
	return (mesh->index_format != INDEX_FORMAT_NONE) ? mesh->index_format : INDEX_FORMAT32;
}

bool mesh_pack_into(const Mesh *mesh, const VertexLayout *layout, void *output, uint64_t output_size);

Mesh mesh_ellipsoid(Arena *arena, float3 origin, float3 radius, uint32_t segments, uint32_t rings);
