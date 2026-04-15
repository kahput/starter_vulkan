#include "mesh_source.h"
#include "core/cmath.h"

#include <core/debug.h>
#include <assets/asset_types.h>

#include <string.h>

MeshSource mesh_source_cube_face_create(Arena *arena, float x, float y, float z, uint8_t orientation) {
	MeshSource rv = {
		.vertex_size = sizeof(Vertex),
		.vertex_count = 6,
	};

	Vertex *vertices = arena_push_count(arena, rv.vertex_count, Vertex);
	rv.vertices = (uint8_t *)vertices;

	/**
				5---4
			   /|  /|
			  0---1 |
			  | 7-|-6
			  |/  |/
			  2---3
	**/

	const float32x3 positions[8] = {
		[0] = { x + -0.5f, y + 0.5f, z + 0.5f },
		[1] = { x + 0.5f, y + 0.5f, z + 0.5f },
		[2] = { x + -0.5f, y + -0.5f, z + 0.5f },
		[3] = { x + 0.5f, y + -0.5f, z + 0.5f },
		[4] = { x + 0.5f, y + 0.5f, z + -0.5f },
		[5] = { x + -0.5f, y + 0.5f, z + -0.5f },
		[6] = { x + 0.5f, y + -0.5f, z + -0.5f },
		[7] = { x + -0.5f, y + -0.5f, z + -0.5f }
	};

	const float32x3 normals[6] = {
		[CUBE_FACE_RIGHT] = { 1.0f, 0.0f, 0.0f },
		[CUBE_FACE_LEFT] = { -1.0f, 0.0f, 0.0f },
		[CUBE_FACE_TOP] = { 0.0f, 1.0f, 0.0f },
		[CUBE_FACE_BOTTOM] = { 0.0f, -1.0f, 0.0f },
		[CUBE_FACE_FRONT] = { 0.0f, 0.0f, 1.0f },
		[CUBE_FACE_BACK] = { 0.0f, 0.0f, -1.0f }
	};

	const float32x2 uvs[6] = {
		{ 0.0f, 0.0f },
		{ 0.0f, 1.0f },
		{ 1.0f, 0.0f },
		{ 1.0f, 0.0f },
		{ 0.0f, 1.0f },
		{ 1.0f, 1.0f }
	};

	const uint8_t indices[6][6] = {
		[CUBE_FACE_RIGHT] = { 1, 3, 4, 4, 3, 6 },
		[CUBE_FACE_LEFT] = { 5, 7, 0, 0, 7, 2 },
		[CUBE_FACE_TOP] = { 1, 4, 0, 0, 4, 5 },
		[CUBE_FACE_BOTTOM] = { 2, 7, 3, 3, 7, 6 },
		[CUBE_FACE_FRONT] = { 0, 2, 1, 1, 2, 3 },
		[CUBE_FACE_BACK] = { 4, 6, 5, 5, 6, 7 }
	};

	for (int face_index = 0; face_index < 6; face_index++) {
		int index = indices[orientation][face_index];

		vertices[face_index].position = positions[index];
		vertices[face_index].normal = normals[orientation];
		vertices[face_index].uv = uvs[face_index];
        vertices[face_index].tangent = (float4){ 0 };
        vertices[face_index].color = (float4){ 0 };
	}

	return rv;
}

void mesh_source_list_push(Arena *arena, MeshSourceList *list, MeshSource source) {
	MeshSourceNode *node = arena_push_struct(arena, MeshSourceNode);
	node->mesh = source;
	node->next = NULL;

	if (list->first == NULL) {
		list->first = node;
		node->next = node;
		node->prev = node;
		list->vertex_size = source.vertex_size;
		list->index_size = source.index_size;
	} else {
		list->first->prev->next = node;
		node->prev = list->first->prev;

		list->first->prev = node;
		node->next = list->first;
	}

	list->count++;
	ASSERT(list->vertex_size == source.vertex_size);

	list->total_vertices_size += source.vertex_count * source.vertex_size;
	list->vertex_count += source.vertex_count;

	list->total_indices_size += source.index_count * source.index_size;
	list->index_count += source.index_count;
}

MeshSource mesh_source_list_flatten(Arena *arena, MeshSourceList *list) {
	MeshSource rv = {
		.vertex_size = list->vertex_size,
		.index_size = list->index_size,
	};
	if (list->count == 0)
		return rv;

    uint8_t *vertices = arena_push_count(arena, list->total_vertices_size, uint8_t);
    size_t vertices_cursor = 0;
    
    uint8_t *indices = arena_push_count(arena, list->total_indices_size, uint8_t);
    size_t indices_cursor = 0;

	MeshSourceNode *node = list->first;
	ASSERT(node);
	do {
		ASSERT(node->mesh.vertex_size == list->vertex_size);
        size_t vertices_size = node->mesh.vertex_count * node->mesh.vertex_size;
		memory_copy(vertices + vertices_cursor, node->mesh.vertices, vertices_size);

        if (list->total_indices_size > 0) {

            ASSERT(node->mesh.index_size == list->index_size);
            size_t indices_size = node->mesh.index_count * node->mesh.index_size;
            memory_copy(indices + indices_cursor, node->mesh.indices, indices_size);

            for (uint32_t byte_offset = 0; byte_offset < node->mesh.index_count * node->mesh.index_size; byte_offset += node->mesh.index_size) {
                uint8_t *submesh_indices = indices + indices_cursor;
                switch (node->mesh.index_size) {
                    case 1:
                        submesh_indices[byte_offset] += vertices_cursor / rv.vertex_size;
                        break;
                    case 2:
                        *(uint16_t *)(submesh_indices + byte_offset) += vertices_cursor / rv.vertex_size;
                        break;
                    case 4:
                        *(uint32_t *)(submesh_indices + byte_offset) += vertices_cursor / rv.vertex_size;
                        break;
                    default:
                        ASSERT_MESSAGE(false, "Unsupported index size");
                        break;
                }
            }

            indices_cursor += indices_size;
        }

		vertices_cursor += vertices_size;

		rv.vertex_count += node->mesh.vertex_count;
		rv.index_count += node->mesh.index_count;

		node = node->next;
	} while (node != list->first);

	rv.vertices = vertices;
	rv.indices = indices;

	return rv;
}
