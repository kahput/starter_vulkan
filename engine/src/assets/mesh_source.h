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

/* typedef enum { */
/* 	ATTRIBUTE_TYPE_POSITION, */
/* 	ATTRIBUTE_TYPE_NORMAL, */
/* 	ATTRIBUTE_TYPE_TANGENT, */
/* 	ATTRIBUTE_TYPE_TEXCOORD, */
/* 	ATTRIBUTE_TYPE_COLOR, */
/* 	ATTRIBUTE_TYPE_JOINTS, */
/* 	ATTRIBUTE_TYPE_WEIGHTS, */
/* } ShaderAttribute; */

enum {
	CUBE_FACE_RIGHT,
	CUBE_FACE_LEFT,
	CUBE_FACE_TOP,
	CUBE_FACE_BOTTOM,
	CUBE_FACE_FRONT,
	CUBE_FACE_BACK,
	CUBE_FACE_COUNT,
};

// MeshSource mesh_source_cube_create(Arena *arena);
ENGINE_API MeshSource mesh_source_cube_face(Arena *arena, float x, float y, float z, uint8_t face_index);
ENGINE_API MeshSource mesh_source_cube(Arena *arena, float x, float y, float z);

typedef struct mesh_source_node {
	struct mesh_source_node *next;
	struct mesh_source_node *prev;

	MeshSource mesh;
} MeshSourceNode;

typedef struct mesh_source_list {
	MeshSourceNode *first;
	uint32_t count;

	size_t total_vertices_size;
	size_t vertex_size;
	uint32_t vertex_count;

	size_t total_indices_size;
	size_t index_size;
	uint32_t index_count;
} MeshSourceList;

ENGINE_API void mesh_source_list_push(Arena *arena, MeshSourceList *list, MeshSource source);
ENGINE_API MeshSource mesh_source_list_flatten(Arena *arena, MeshSourceList *list);

#endif /* MESH_SOURCE_H_ */
