#include "ecs.h"
#include "assets/json_parser.h"
#include "common.h"
#include "core/arena.h"
#include "core/debug.h"
#include "core/logger.h"
#include "platform/filesystem.h"

struct ECS {
	Arena *arena;

	uint32_t flags[MAX_ENTITIES];
	Buffer components[COMPONENT_TYPE_MAX];

	uint32_t entity_count, highest_valid;
};

static bool serialize_entity(ECS *world, JsonExporter *exporter, Entity entity);
static Entity deserialize_entity(ECS *world, JsonNode *root);

ECS *ecs_make(Arena *arena) {
	ECS *ecs = arena_push_struct(arena, ECS);
	ecs->arena = arena;

	for (uint32_t index = 1; index < COMPONENT_TYPE_MAX; ++index) {
		ComponentMetadata data = component_metadata[index];
		ecs->components[index].size = data.element_size * MAX_ENTITIES;
		ecs->components[index].pointer = arena_push(arena, ecs->components[index].size, data.element_align, true);
	}

	return ecs;
}
ECS *ecs_make_copy(Arena *arena, ECS *src) {
	ECS *result = ecs_make(arena);
	result->entity_count = src->entity_count;
	result->highest_valid = src->highest_valid;

	for (uint32_t type_id = 1; type_id < COMPONENT_TYPE_MAX; ++type_id)
		memory_copy(result->components[type_id].pointer, src->components[type_id].pointer, src->components[type_id].size);

	memory_copy(result->flags, src->flags, sizeof(src->flags));

	return result;
}

bool ecs_valid(ECS *world, Entity entity) {
	if (entity != 0 && entity < MAX_ENTITIES && world->flags[entity] != 0)
		return true;
	return false;
}

static inline bool component_valid(ComponentID type_id) {
	if (type_id == 0 || type_id >= COMPONENT_TYPE_MAX)
		return false;
	return true;
}

static inline uint32_t component_flag(ComponentID id) {
	ASSERT(id < INT32_MAX);
	if (id == 0)
		return 0;
	return (1ULL << (id - 1));
}

Entity ecs_spawn(ECS *world, float3 position) {
	for (Entity entity = 1; entity < MAX_ENTITIES; ++entity) {
		if (world->flags[entity] == 0) {
			world->flags[entity] = component_flag(COMPONENT_TYPE_TransformComponent);
			ecs_put(world, entity, TransformComponent,
				{
				  .position = position,
				  .scale = FLOAT3_ONE,
				});

			if (world->highest_valid < entity)
				world->highest_valid = entity;
			world->entity_count++;
			return entity;
		}
	}

	ASSERT(false);
	return 0;
}

Entity ecs_copy(ECS *world, Entity target) {
	if (ecs_valid(world, target) == false) {
		LOG_WARN("ecs_copy - invalid entity passed, aborting");
		return 0;
	}
	Entity entity = ecs_spawn(world, FLOAT3_ZERO);

	for (uint32_t type_id = 1; type_id < COMPONENT_TYPE_MAX; ++type_id) {
		if (FLAG_GET(world->flags[target], component_flag(type_id)) == false)
			continue;

		ComponentMetadata metadata = component_metadata[type_id];

		void *dst = ecs_push_id(world, entity, type_id);
		void *src = ecs_push_id(world, target, type_id);
		memory_copy(dst, src, metadata.element_size);
	}

	return entity;
}

void ecs_despawn(ECS *world, Entity entity) {
	if (ecs_valid(world, entity)) {
		for (uint32_t index = 0; index < COMPONENT_TYPE_MAX; ++index) {
			ComponentMetadata metadata = component_metadata[index];
			void *pointer = world->components[index].pointer + metadata.element_size * entity;

			memory_zero(pointer, metadata.element_size);
		}

		world->flags[entity] = 0;
		world->entity_count--;
	}
}

void *ecs_push_id(ECS *world, Entity entity, ComponentID type_id) {
	// NOTE: Maybe return entity 0 component data instead?
	if (ecs_valid(world, entity) == false)
		return NULL;
	if (component_valid(type_id) == false) {
		LOG_WARN("ecs_push_id - invalid type_id of %d passed, aborting", type_id);
		return NULL;
	}

	ComponentMetadata metadata = component_metadata[type_id];
	void *pointer = world->components[type_id].pointer + metadata.element_size * entity;
	world->flags[entity] |= component_flag(type_id);

	return pointer;
}

void *ecs_find_id(ECS *world, Entity entity, ComponentID type_id) {
	if (ecs_valid(world, entity) == false)
		return NULL;
	if (component_valid(type_id) == false) {
		LOG_WARN("ecs_push_id - invalid type_id of %d passed, aborting", type_id);
		return NULL;
	}

	if (FLAG_GET(world->flags[entity], component_flag(type_id)) == false)
		return NULL;

	ComponentMetadata metadata = component_metadata[type_id];

	void *pointer = world->components[type_id].pointer + metadata.element_size * entity;
	return pointer;
}

void ecs_pop_id(ECS *world, Entity entity, ComponentID type_id) {
	if (component_valid(type_id) == false) {
		LOG_WARN("ecs_push_id - invalid type_id of %d passed, aborting", type_id);
		return;
	}
	if (ecs_valid(world, entity) == false || FLAG_GET(world->flags[entity], component_flag(type_id)) == false)
		return;

	ComponentMetadata metadata = component_metadata[type_id];
	void *pointer = world->components[type_id].pointer + metadata.element_size * entity;
	memory_zero(pointer, metadata.element_size);

	world->flags[entity] &= ~component_flag(type_id);
}

bool ecs_has_id(ECS *world, Entity entity, ComponentID type_id) {
	if (component_valid(type_id) == false) {
		LOG_WARN("ecs_push_id - invalid type_id of %d passed, aborting", type_id);
		return false;
	}
	if (ecs_valid(world, entity) == false || FLAG_GET(world->flags[entity], component_flag(type_id)) == false)
		return false;

	return true;
}

void ecs_hierarchy_parent(ECS *world, Entity parent_entity, Entity child_entity) {
	HierarchyComponent *parent = ecs_has(world, parent_entity, HierarchyComponent)
		? ecs_find(world, parent_entity, HierarchyComponent)
		: ecs_push(world, parent_entity, HierarchyComponent);

	HierarchyComponent *child = ecs_has(world, child_entity, HierarchyComponent)
		? ecs_find(world, child_entity, HierarchyComponent)
		: ecs_push(world, child_entity, HierarchyComponent);

	ecs_hierarchy_unparent(world, child_entity);

	child->parent = parent_entity;
	child->next_sibling = parent->first_child;

	if (parent->first_child) {
		HierarchyComponent *old_first = ecs_find(world, parent->first_child, HierarchyComponent);
		old_first->prev_sibling = child_entity;
	}

	parent->first_child = child_entity;
}

void ecs_hierarchy_unparent(ECS *world, Entity entity) {
	if (ecs_has(world, entity, HierarchyComponent) == false)
		return;

	HierarchyComponent *node = ecs_find(world, entity, HierarchyComponent);
	if (node->parent == 0)
		return;

	HierarchyComponent *parent = ecs_find(world, node->parent, HierarchyComponent);

	if (node->prev_sibling) {
		HierarchyComponent *prev = ecs_find(world, node->prev_sibling, HierarchyComponent);
		prev->next_sibling = node->next_sibling;
	} else if (parent->first_child == entity)
		parent->first_child = node->next_sibling;

	if (node->next_sibling) {
		HierarchyComponent *next = ecs_find(world, node->next_sibling, HierarchyComponent);
		next->prev_sibling = node->prev_sibling;
	}

	node->parent = 0;
	node->next_sibling = 0;
	node->prev_sibling = 0;
}

void ecs_hierarchical_despawn(ECS *world, Entity root) {
	if (ecs_has(world, root, HierarchyComponent) == false) {
		ecs_despawn(world, root);
		return;
	}

	ecs_hierarchy_unparent(world, root);

	Entity child = ecs_find(world, root, HierarchyComponent)->first_child;
	while (child) {
		uint32_t next_child = ecs_find(world, child, HierarchyComponent)->next_sibling;
		ecs_hierarchical_despawn(world, child);
		child = next_child;
	}

	ecs_despawn(world, root);
}

Entity ecs_hierarchical_copy(ECS *world, Entity root) {
	if (ecs_has(world, root, HierarchyComponent) == false)
		return ecs_copy(world, root);

	Entity copy = ecs_copy(world, root);
	HierarchyComponent *hierarchy = ecs_find(world, root, HierarchyComponent);
	ecs_hierarchy_parent(world, hierarchy->parent, copy);

	for (Entity child = hierarchy->first_child; child; child = hierarchy->next_sibling) {
		ecs_hierarchy_parent(world, copy, ecs_hierarchical_copy(world, child));
		hierarchy = ecs_find(world, child, HierarchyComponent);
	}

	return copy;
}

void ecs_serialize_entity(ECS *world, Entity root, String output_path) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	JsonExporter exporter = json_exporter_make(scratch.arena);
	serialize_entity(world, &exporter, root);

	File file = filesystem_open(output_path, FILE_MODE_WRITE);

	file_write(&file, 1, exporter.arena->offset - exporter.start_offset, (uint8_t *)exporter.arena->base + exporter.start_offset);
	file_close(&file);

	arena_scratch_end(scratch);
}

Entity ecs_deserialize_entity(ECS *world, String path) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	JsonNode *root = json_parse(scratch.arena,
		string_wrap_buffer(
			filesystem_read(scratch.arena,
				path)));

	Entity result = deserialize_entity(world, root);
	arena_scratch_end(scratch);

	return result;
}

EcsIterator ecs_query_make(ECS *world, uint32_t count, ComponentID *component_ids) {
	EcsIterator result = { .world = world, .current = 1 };

	for (uint32_t index = 0; index < count; ++index) {
		ComponentID type_id = component_ids[index];
		ASSERT(type_id < COMPONENT_TYPE_MAX);
		/* ASSERT(component_valid(type_id)); */

		result.mask |= component_flag(type_id);
	}

	return result;
}

Entity ecs_next(EcsIterator *it) {
	while (it->current <= it->world->highest_valid) {
		Entity entity = it->current++;
		if (FLAG_GET(it->world->flags[entity], it->mask)) {
			return entity;
		}
	}

	return 0;
}

// Serialization
bool serialize_entity(ECS *world, JsonExporter *exporter, Entity entity) {
	ASSERT(ecs_valid(world, entity));

	json_begin_map(exporter, S(""));
	json_write_pair(exporter, S("name"), String, S("entity"));

	if (world->flags[entity] != 0) {
		json_begin_map(exporter, S("componenets"));

		if (ecs_has(world, entity, TransformComponent)) {
			TransformComponent *transform = ecs_find(world, entity, TransformComponent);
			json_begin_map(exporter, S("transform"));

			json_begin_array(exporter, S("position"));
			json_write_float3(exporter, transform->position);
			json_end_array(exporter);

			json_begin_array(exporter, S("rotation"));
			json_write_float3(exporter, transform->rotation);
			json_end_array(exporter);

			json_begin_array(exporter, S("scale"));
			json_write_float3(exporter, transform->scale);
			json_end_array(exporter);

			json_end_map(exporter);
		}

		if (ecs_has(world, entity, MeshComponent)) {
			MeshComponent *mesh = ecs_find(world, entity, MeshComponent);
			json_begin_map(exporter, S("mesh"));
			json_write_pair(exporter, S("asset_id"), uint64_t, mesh->group_id);
			json_end_map(exporter);
		}

		if (ecs_has(world, entity, ColliderComponent)) {
			ColliderComponent *collider = ecs_find(world, entity, ColliderComponent);

			json_begin_map(exporter, S("collider"));

			json_begin_array(exporter, S("center"));
			json_write_float3(exporter, collider->aabb.center);
			json_end_array(exporter);

			json_begin_array(exporter, S("extent"));
			json_write_float3(exporter, collider->aabb.extent);
			json_end_array(exporter);

			json_end_map(exporter);
		}

		json_end_map(exporter);

		if (ecs_has(world, entity, HierarchyComponent)) {
			HierarchyComponent *hierarchy = ecs_find(world, entity, HierarchyComponent);
			if (hierarchy->first_child) {
				json_begin_array(exporter, S("children"));
				Entity child = hierarchy->first_child;
				while (child) {
					serialize_entity(world, exporter, child);

					if (ecs_has(world, child, HierarchyComponent)) {
						HierarchyComponent *child_hierarchy = ecs_find(world, child, HierarchyComponent);
						child = child_hierarchy->next_sibling;
					} else
						child = 0;
				}
				json_end_array(exporter);
			}
		}
	}

	json_end_map(exporter);
	return true;
}

Entity deserialize_entity(ECS *world, JsonNode *root) {
	if (root == NULL)
		return 0;

	ArenaTemp scratch = arena_scratch_begin(NULL);
	Entity entity = ecs_spawn(world, FLOAT3_ZERO);

	String name = json_find(root, S("name"), String);
	JsonNode *components = json_node(root, S("componenets"));

	if (components) {
		JsonNode *transform_node = json_node(components, S("transform"));
		if (transform_node) {
			Transform3 *transform = ecs_push(world, entity, TransformComponent);

			uint32_t index = 0;
			for (JsonNode *node = json_list(transform_node, S("position")); node; node = node->next, index++) {
				float value = json_as(node, float);
				((float *)&transform->position)[index] = value;
			}

			index = 0;
			for (JsonNode *node = json_list(transform_node, S("rotation")); node; node = node->next, index++) {
				float value = json_as(node, float);
				((float *)&transform->rotation)[index] = value;
			}

			index = 0;
			for (JsonNode *node = json_list(transform_node, S("scale")); node; node = node->next, index++) {
				float value = json_as(node, float);
				((float *)&transform->scale)[index] = value;
			}
		}

		JsonNode *mesh_node = json_node(components, S("mesh"));
		if (mesh_node) {
			MeshComponent *mesh = ecs_push(world, entity, MeshComponent);
			mesh->group_id = json_find(mesh_node, S("asset_id"), uint64_t);
		}

		JsonNode *collider_node = json_node(components, S("collider"));
		if (collider_node) {
			ColliderComponent *collider = ecs_push(world, entity, ColliderComponent);

			uint32_t index = 0;
			for (JsonNode *node = json_list(transform_node, S("center")); node; node = node->next, index++) {
				float value = json_as(node, float);
				((float *)&collider->aabb.center)[index] = value;
			}

			index = 0;
			for (JsonNode *node = json_list(transform_node, S("extent")); node; node = node->next, index++) {
				float value = json_as(node, float);
				((float *)&collider->aabb.extent)[index] = value;
			}
		}
	}

	for (JsonNode *node = json_list(root, S("children")); node; node = node->next)
		ecs_hierarchy_parent(world, entity, deserialize_entity(world, node));

	arena_scratch_end(scratch);
	return entity;
}
