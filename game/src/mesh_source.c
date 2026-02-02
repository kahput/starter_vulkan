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

	Vertex *vertices = arena_push_array_zero(arena, Vertex, rv.vertex_count);
	rv.vertices = (uint8_t *)vertices;

	/**
				5---4
			   /|  /|
			  0---1 |
			  | 7-|-6
			  |/  |/
			  2---3
	**/

	const Vector3f positions[8] = {
		[0] = { x + -0.5f, y + 0.5f, z + 0.5f },
		[1] = { x + 0.5f, y + 0.5f, z + 0.5f },
		[2] = { x + -0.5f, y + -0.5f, z + 0.5f },
		[3] = { x + 0.5f, y + -0.5f, z + 0.5f },
		[4] = { x + 0.5f, y + 0.5f, z + -0.5f },
		[5] = { x + -0.5f, y + 0.5f, z + -0.5f },
		[6] = { x + 0.5f, y + -0.5f, z + -0.5f },
		[7] = { x + -0.5f, y + -0.5f, z + -0.5f }
	};

	const Vector3f normals[6] = {
		[CUBE_FACE_RIGHT] = { 1.0f, 0.0f, 0.0f },
		[CUBE_FACE_LEFT] = { -1.0f, 0.0f, 0.0f },
		[CUBE_FACE_TOP] = { 0.0f, 1.0f, 0.0f },
		[CUBE_FACE_BOTTOM] = { 0.0f, -1.0f, 0.0f },
		[CUBE_FACE_FRONT] = { 0.0f, 0.0f, 1.0f },
		[CUBE_FACE_BACK] = { 0.0f, 0.0f, -1.0f }
	};

	const Vector2f uvs[6] = {
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
	}

	return rv;
}

void mesh_source_list_push(Arena *arena, MeshList *list, MeshSource source) {
	MeshNode *node = arena_push_struct(arena, MeshNode);
	node->source = source;
	node->next = NULL;

	if (list->first == NULL) {
		list->first = node;
		list->vertex_size = source.vertex_size;
		list->last = node;
	} else {
		list->last->next = node;
		list->last = node;
	}

	list->node_count++;
	ASSERT(list->vertex_size == source.vertex_size);

	list->total_vertices_size += source.vertex_count * source.vertex_size;
	list->vertex_count += source.vertex_count;

	list->total_indices_size += source.index_count * source.index_size;
	list->index_count += source.index_count;
}

MeshSource mesh_source_list_flatten(Arena *arena, MeshList *list) {
	MeshSource rv = { 0 };
	if (list->node_count == 0)
		return rv;

	uint8_t *vertices = arena_push_array(arena, uint8_t, list->total_vertices_size);
	size_t cursor = 0;

	MeshNode *node = list->first;
	while (node) {
		size_t size = node->source.vertex_count * node->source.vertex_size;
		memcpy(vertices + cursor, node->source.vertices, size);

		cursor += size;
		rv.vertex_count += node->source.vertex_count;
		node = node->next;
	}

	rv.vertex_size = list->vertex_size;
	rv.vertices = vertices;
	return rv;
}
