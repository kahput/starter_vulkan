#include "mesh.h"
#include "core/cmath.h"
#include "core/debug.h"

#include "geom.h"

uint32_t data_format_sizes[DATA_FORMAT_MAX] = {
	[DATA_FORMAT_FLOAT] = 4 * 1,
	[DATA_FORMAT_FLOAT2] = 4 * 2,
	[DATA_FORMAT_FLOAT3] = 4 * 3,
	[DATA_FORMAT_FLOAT4] = 4 * 4,

	[DATA_FORMAT_INT4] = 4 * 4,

};

uint32_t data_format_alignments[DATA_FORMAT_MAX] = {
	[DATA_FORMAT_FLOAT] = 4,
	[DATA_FORMAT_FLOAT2] = 8,
	[DATA_FORMAT_FLOAT3] = 16,
	[DATA_FORMAT_FLOAT4] = 16,

	[DATA_FORMAT_INT4] = 16,
};

bool vertex_layout_push(VertexLayout *layout, VertexSemantic semantic, DataFormat format) {
	bool ok = layout && layout->attribute_count < countof(layout->attributes) && format && vertex_layout_has(layout, semantic) == false;

	if (ok) {
		layout->attributes[layout->attribute_count++] = (VertexAttribute){ semantic, format };
		layout->stride += data_format_to_size(format);

		/* uint32_t offset = 0; */
		/* uint32_t max_alignment = 1; */

		/* for (uint32_t attribute_index = 0; attribute_index < layout->attribute_count; ++attribute_index) { */
		/* 	DataFormat format = layout->attributes[attribute_index].format; */

		/* 	uint32_t alignment = data_format_to_alignment(format); */
		/* 	uint32_t size = data_format_to_size(format); */

		/* 	offset = alignup(offset, alignment); */
		/* 	layout->attribute_offsets[attribute_index] = offset; */

		/* 	offset += size; */
		/* 	max_alignment = MAX(max_alignment, alignment); */
		/* } */

		/* layout->stride = alignup(offset, max_alignment); */
	}

	return ok;
}
VertexLayout vertex_layout_from_array(const VertexAttribute *attributes, uint32_t attribute_count) {
	VertexLayout result = { 0 };
	bool ok = attributes;
	if (ok) {
		for (uint32_t index = 0; index < attribute_count; ++index) {
			vertex_layout_push(&result, attributes[index].semantic, attributes[index].format);
		}
	}

	return result;
}

DataFormat vertex_semantic_default_format[VERTEX_SEMANTIC_MAX] = {
	[VERTEX_SEMANTIC_POSITION] = DATA_FORMAT_FLOAT3,
	[VERTEX_SEMANTIC_NORMAL] = DATA_FORMAT_FLOAT3,
	[VERTEX_SEMANTIC_TANGENT] = DATA_FORMAT_FLOAT3,
	[VERTEX_SEMANTIC_UV0] = DATA_FORMAT_FLOAT2,
	[VERTEX_SEMANTIC_UV1] = DATA_FORMAT_FLOAT2,
	/* [VERTEX_SEMANTIC_COLOR0] = DATA_FORMAT_NONE, */
	[VERTEX_SEMANTIC_JOINTS] = DATA_FORMAT_INT4,
	[VERTEX_SEMANTIC_WEIGHTS] = DATA_FORMAT_FLOAT4,
};

void *vertex_semantic_default_value[VERTEX_SEMANTIC_MAX] = {
	[VERTEX_SEMANTIC_NORMAL] = &(float3){ 0.0f, 0.0f, 1.0f },
	[VERTEX_SEMANTIC_TANGENT] = &(float3){ 1.0f, 0.0f, 0.0f },
	[VERTEX_SEMANTIC_UV0] = &(float2){ 0 },
	[VERTEX_SEMANTIC_UV1] = &(float2){ 0 },
	/* [VERTEX_SEMANTIC_COLOR0] = DATA_FORMAT_NONE, */
	[VERTEX_SEMANTIC_JOINTS] = &(int4){ 0 },
	[VERTEX_SEMANTIC_WEIGHTS] = &(float4){ 0 },
};

bool mesh_pack_into(const Mesh *mesh, const VertexLayout *layout, void *output, uint64_t output_size) {
	bool ok = mesh && layout && output && output_size == mesh->total_vertex_count * layout->stride;
	if (ok) {
		uint32_t offset = 0;
		for (uint32_t attribute_index = 0; attribute_index < layout->attribute_count; ++attribute_index) {
			VertexAttribute at = layout->attributes[attribute_index];
			uint32_t layout_element_size = data_format_to_size(layout->attributes[attribute_index].format);

			uint8_t *src = mesh->streams[at.semantic];
			uint8_t *dst = (uint8_t *)output + offset;

			offset += layout_element_size;
			if (src == 0) {
				void *default_value = vertex_semantic_default_value[at.semantic];
				uint32_t default_element_size = data_format_to_size(vertex_semantic_default_format[attribute_index]);
				uint32_t copy_size = MIN(layout_element_size, default_element_size);

				for (uint32_t vertex_index = 0; vertex_index < mesh->total_vertex_count; ++vertex_index)
					memory_copy(dst + vertex_index * layout->stride, default_value, copy_size);
			} else {
				DataFormat stream_format = mesh_stream_format(mesh, at.semantic); // resolves DEFAULT
				uint32_t stream_element_size = data_format_to_size(stream_format);
				uint32_t copy_size = MIN(layout_element_size, stream_element_size);

				for (uint32_t vertex_index = 0; vertex_index < mesh->total_vertex_count; ++vertex_index)
					memory_copy(dst + vertex_index * layout->stride, src + vertex_index * stream_element_size, copy_size);
			}
		}
	}

	ASSERT(ok);
	return ok;
}

Mesh mesh_ellipsoid(Arena *arena, float3 origin, float3 radius, uint32_t segments, uint32_t rings) {
	Mesh result = { 0 };
	bool ok = arena;
	if (ok) {
		rings = MAX(rings, 1);
		rings += 2; // end caps
		segments = MAX(segments, 4) + 1; // duplicate seam segment

		result.total_vertex_count = rings * segments;
		result.bounds = aabb3_empty();

		float3 *positions = result.streams[VERTEX_SEMANTIC_POSITION] =
			arena_push_count(arena, float3, result.total_vertex_count);
		float3 *normals = result.streams[VERTEX_SEMANTIC_NORMAL] =
			arena_push_count(arena, float3, result.total_vertex_count);
		float2 *uvs = result.streams[VERTEX_SEMANTIC_UV0] =
			arena_push_count(arena, float2, result.total_vertex_count);

		uint32_t vertex_cursor = 0;
		for (uint32_t ring = 0; ring < rings; ++ring) {
			float theta = ((float)ring / (rings - 1)) * PIf;
			float ct = cosf(theta), st = sinf(theta);

			for (uint32_t segment = 0; segment < segments; ++segment) {
				float azimuth = ((float)segment / (segments - 1)) * TAU;
				float ca = cosf(azimuth), sa = -sinf(azimuth);

				positions[vertex_cursor] = add3(origin, float3(st * ca * radius.x, ct * radius.y, st * sa * radius.z));
				normals[vertex_cursor] = float3(st * ca, ct, st * sa);
				uvs[vertex_cursor] = float2((float)segment / (segments - 1), 1.0f - (float)ring / (rings - 1));

				aabb3_expand(&result.bounds, positions[vertex_cursor]);
				vertex_cursor++;
			}
		}

		uint32_t face_count = (segments - 1) * (rings - 1);
		result.total_index_count = face_count * 6;
		result.indices = arena_push_count(arena, uint32_t, result.total_index_count);

		uint32_t *index_cursor = result.indices;
		for (uint32_t face = 0; face < face_count; ++face) {
			uint32_t ring = face / (segments - 1);
			uint32_t segment = face % (segments - 1);
			uint32_t index = ring * segments + segment;

			index_cursor[0] = index;
			index_cursor[1] = index + segments;
			index_cursor[2] = index + segments + 1;

			index_cursor[3] = index;
			index_cursor[4] = index + segments + 1;
			index_cursor[5] = index + 1;

			index_cursor += 6;
		}
	}

	return result;
}
