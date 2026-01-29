#include "mesh_source.h"

#include <core/debug.h>

#include <string.h>

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
