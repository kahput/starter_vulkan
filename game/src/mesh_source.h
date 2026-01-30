#ifndef MESH_SOURCE_H_
#define MESH_SOURCE_H_

#include <common.h>
#include <core/arena.h>

typedef struct mesh_source {
	uint8_t *vertices;
	size_t vertex_size;
	uint32_t vertex_count;

	uint8_t *indices;
	size_t index_size;
	uint32_t index_count;
} MeshSource;

enum {
	CUBE_FACE_RIGHT,
	CUBE_FACE_LEFT,
	CUBE_FACE_TOP,
	CUBE_FACE_BOTTOM,
	CUBE_FACE_FRONT,
	CUBE_FACE_BACK
};

// MeshSource mesh_source_cube_create(Arena *arena);
MeshSource mesh_source_cube_face_create(Arena *arena, float x, float y, float z, uint8_t face_index);

MeshSource mesh_source_sphere_create(Arena *arena, float radius);

typedef struct MeshNode {
	struct MeshNode *next;
	MeshSource source;
} MeshNode;

typedef struct MeshList {
	MeshNode *first;
	MeshNode *last;
	uint32_t node_count;

	size_t total_vertices_size;
	size_t vertex_size;
	uint32_t vertex_count;

	size_t total_indices_size;
	size_t index_size;
	uint32_t index_count;
} MeshList;

void mesh_source_list_push(Arena *arena, MeshList *list, MeshSource source);
MeshSource mesh_source_list_flatten(Arena *arena, MeshList *list);

#endif /* MESH_SOURCE_H_ */
