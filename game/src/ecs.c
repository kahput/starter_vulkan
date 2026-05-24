#include "ecs.h"
#include "assets/json_parser.h"
#include "common.h"
#include "core/arena.h"
#include "core/debug.h"
#include "core/logger.h"
#include "platform/filesystem.h"

struct ECS {
	Arena *arena;

	ComponentBitset bitset[MAX_ENTITIES];
	Buffer components[COMPONENT_TYPE_MAX];

	uint32_t entity_count, highest_valid;
};

static bool serialize_entity(ECS *world, JsonExporter *exporter, Entity entity);
static Entity deserialize_entity(ECS *world, JsonNode *root);

ECS *ecs_make(Arena *arena) {
	ECS *ecs = arena_push_struct(arena, ECS);
	ecs->arena = arena;
	memory_zero_array(ecs->components);

	for (uint32_t index = 0; index < COMPONENT_TYPE_MAX; ++index) {
		ComponentMetadata data = component_metadata[index];
		ecs->components[index].size = data.element_size * MAX_ENTITIES;

		if (data.element_size)
			ecs->components[index].memory = arena_push(arena, ecs->components[index].size, data.element_align, true);
	}

	return ecs;
}
ECS *ecs_make_copy(Arena *arena, ECS *src) {
	ECS *result = ecs_make(arena);
	result->entity_count = src->entity_count;
	result->highest_valid = src->highest_valid;

	for (uint32_t type_id = 1; type_id < COMPONENT_TYPE_MAX; ++type_id)
		memory_copy(result->components[type_id].memory, src->components[type_id].memory, src->components[type_id].size);

	memory_copy_array(result->bitset, src->bitset);
	return result;
}

static inline bool _ecs_bitset_zero(ComponentBitset bitset) {
	for (uint32_t index = 0; index < ECS_BITSET_SIZE; ++index) {
		if (bitset[index] != 0)
			return false;
	}

	return true;
}

static inline void _ecs_bitset_flip(ComponentBitset bitset, ComponentID id, bool on) {
	uint32_t index = id / ECS_BITSET_WIDTH;

	if (on)
		bitset[index] |= 1ULL << (id % ECS_BITSET_WIDTH);
	else
		bitset[index] &= ~(1ULL << (id % ECS_BITSET_WIDTH));
}

static inline bool _ecs_bitset_test(ComponentBitset bitset, ComponentID id) {
	uint32_t index = id / ECS_BITSET_WIDTH;
	return bitset[index] & (1ULL << (id % ECS_BITSET_WIDTH));
}

static inline bool _ecs_bitset_mask(ComponentBitset target, ComponentBitset mask) {
	for (uint32_t index = 0; index < ECS_BITSET_SIZE; ++index) {
		if ((target[index] & mask[index]) != mask[index])
			return false;
	}

	return true;
}

static inline bool _ecs_component_valid(ComponentID type_id) {
	if (type_id >= COMPONENT_TYPE_MAX)
		return false;
	return true;
}

bool ecs_valid(ECS *world, Entity entity) {
	if (entity == 0 || entity > MAX_ENTITIES || _ecs_bitset_zero(world->bitset[entity]))
		return false;

	return true;
}

Entity ecs_spawn(ECS *world, float3 position) {
	for (Entity entity = 1; entity < MAX_ENTITIES; ++entity) {
		if (_ecs_bitset_zero(world->bitset[entity])) {
			ecs_enable_id(world, entity, COMPONENT_TAG_ACTIVE);
			ecs_enable_id(world, entity, ecs_type_id(TransformComponent));
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
		if (ecs_has_id(world, target, type_id) == false)
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
			void *pointer = world->components[index].memory + metadata.element_size * entity;

			memory_zero(pointer, metadata.element_size);
		}

		memory_zero_array(world->bitset[entity]);
		world->entity_count--;
	}
}

void ecs_enable(ECS *world, Entity entity) {
	ecs_enable_id(world, entity, COMPONENT_TAG_ACTIVE);
}

void ecs_disable(ECS *world, Entity entity) {
	ecs_disable_id(world, entity, COMPONENT_TAG_ACTIVE);
}

void *ecs_push_id(ECS *world, Entity entity, ComponentID type_id) {
	// NOTE: Maybe return entity 0 component data instead?
	if (ecs_valid(world, entity) == false)
		return NULL;
	if (_ecs_component_valid(type_id) == false) {
		LOG_WARN("ecs_push_id - invalid type_id of %d passed, aborting", type_id);
		return NULL;
	}

	ComponentMetadata metadata = component_metadata[type_id];
	void *pointer = world->components[type_id].memory + metadata.element_size * entity;

	ecs_enable_id(world, entity, type_id);
	return pointer;
}

void *ecs_find_id(ECS *world, Entity entity, ComponentID type_id) {
	if (ecs_valid(world, entity) == false)
		return NULL;
	if (_ecs_component_valid(type_id) == false) {
		LOG_WARN("ecs_push_id - invalid type_id of %d passed, aborting", type_id);
		return NULL;
	}

	if (ecs_has_id(world, entity, type_id) == false)
		return NULL;

	ComponentMetadata metadata = component_metadata[type_id];

	void *pointer = world->components[type_id].memory + metadata.element_size * entity;
	return pointer;
}

void ecs_pop_id(ECS *world, Entity entity, ComponentID type_id) {
	if (_ecs_component_valid(type_id) == false) {
		LOG_WARN("ecs_push_id - invalid type_id of %d passed, aborting", type_id);
		return;
	}
	if (ecs_valid(world, entity) == false || ecs_has_id(world, entity, type_id) == false)
		return;

	ComponentMetadata metadata = component_metadata[type_id];
	void *pointer = world->components[type_id].memory + metadata.element_size * entity;
	memory_zero(pointer, metadata.element_size);

	ecs_disable_id(world, entity, type_id);
}

bool ecs_has_id(ECS *world, Entity entity, ComponentID type_id) {
	if (_ecs_component_valid(type_id) == false) {
		LOG_WARN("ecs_push_id - invalid type_id of %d passed, aborting", type_id);
		return false;
	}
	if (ecs_valid(world, entity) == false)
		return false;

	return _ecs_bitset_test(world->bitset[entity], type_id);
}

bool ecs_has_ids(ECS *world, Entity entity, uint32_t type_count, ComponentID *type_ids) {
	ASSERT(type_count == 0 || type_ids);
	for (uint32_t index = 0; index < type_count; ++index)
		if (ecs_has_id(world, entity, type_ids[index]) == false)
			return false;

	return true;
}

void ecs_disable_id(ECS *world, Entity entity, ComponentID type_id) {
	if (ecs_valid(world, entity) == false || _ecs_component_valid(type_id) == false)
		return;

	_ecs_bitset_flip(world->bitset[entity], type_id, false);
}

void ecs_enable_id(ECS *world, Entity entity, ComponentID type_id) {
	if (_ecs_component_valid(type_id) == false)
		return;
	if (type_id != COMPONENT_TAG_ACTIVE && ecs_valid(world, entity) == false) {
		ASSERT(false);
		return;
	}

	_ecs_bitset_flip(world->bitset[entity], type_id, true);
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

bool ecs_hierarchy_has_child(ECS *world, Entity _parent, Entity _child) {
	if (ecs_has(world, _parent, HierarchyComponent) == false)
		return false;

	HierarchyComponent *parent = ecs_find(world, _parent, HierarchyComponent);
	if (parent->first_child == 0)
		return false;
	if (parent->first_child == _child)
		return true;

	if (ecs_has(world, _child, HierarchyComponent) == false)
		return false;

	HierarchyComponent *child = ecs_find(world, parent->first_child, HierarchyComponent);
	while (child->next_sibling) {
		if (child->next_sibling == _child)
			return true;

		child = ecs_find(world, child->next_sibling, HierarchyComponent);
	}

	return false;
}

bool ecs_hierarchy_has_parent(ECS *world, Entity child) {
	if (ecs_has(world, child, HierarchyComponent) == false)
		return false;

	return ecs_find(world, child, HierarchyComponent)->parent != 0;
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

void ecs_hierarchical_disable(ECS *world, Entity root) {
	if (ecs_has(world, root, HierarchyComponent) == false) {
		ecs_disable(world, root);
		return;
	}

	Entity child = ecs_find(world, root, HierarchyComponent)->first_child;
	while (child) {
		uint32_t next_child = ecs_find(world, child, HierarchyComponent)->next_sibling;
		ecs_hierarchical_disable(world, child);
		child = next_child;
	}

	ecs_disable(world, root);
}

void ecs_hierarchical_enable(ECS *world, Entity root) {
	if (ecs_has(world, root, HierarchyComponent) == false) {
		ecs_enable(world, root);
		return;
	}

	Entity child = ecs_find(world, root, HierarchyComponent)->first_child;
	while (child) {
		uint32_t next_child = ecs_find(world, child, HierarchyComponent)->next_sibling;
		ecs_hierarchical_enable(world, child);
		child = next_child;
	}

	ecs_enable(world, root);
}

void ecs_serialize_entity(ECS *world, Entity root, String output_path) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	JsonExporter exporter = json_exporter_make(scratch.arena);
	serialize_entity(world, &exporter, root);

	File file = filesystem_open(output_path, FILE_MODE_WRITE);

	file_write(&file, exporter.arena->offset - exporter.start_offset, (uint8_t *)exporter.arena->base + exporter.start_offset);
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
		ASSERT(_ecs_component_valid(type_id));

		_ecs_bitset_flip(result.mask, type_id, true);
	}

	return result;
}

Entity ecs_next(EcsIterator *it) {
	while (it->current <= it->world->highest_valid) {
		Entity entity = it->current++;
		if (ecs_has_id(it->world, entity, COMPONENT_TAG_ACTIVE) == false)
			continue;

		if (_ecs_bitset_mask(it->world->bitset[entity], it->mask) == false)
			continue;

		return entity;
	}

	return 0;
}

// Serialization
bool serialize_entity(ECS *world, JsonExporter *exporter, Entity entity) {
	ASSERT(ecs_valid(world, entity));

	json_map_begin(exporter, S(""));
	json_write_pair(exporter, S("name"), String, S("entity"));

	if (ecs_valid(world, entity)) {
		json_map_begin(exporter, S("componenets"));

		if (ecs_has(world, entity, TransformComponent)) {
			TransformComponent *transform = ecs_find(world, entity, TransformComponent);
			json_map_begin(exporter, S("transform"));

			json_array_begin(exporter, S("position"));
			json_write_float3(exporter, transform->position);
			json_array_end(exporter);

			json_array_begin(exporter, S("rotation"));
			json_write_float3(exporter, transform->rotation);
			json_array_end(exporter);

			json_array_begin(exporter, S("scale"));
			json_write_float3(exporter, transform->scale);
			json_array_end(exporter);

			json_map_end(exporter);
		}

		if (ecs_has(world, entity, MeshComponent)) {
			MeshComponent *mesh = ecs_find(world, entity, MeshComponent);
			json_map_begin(exporter, S("mesh"));
			json_write_pair(exporter, S("asset_id"), uint64_t, mesh->group_id);
			json_map_end(exporter);
		}

		if (ecs_has(world, entity, ColliderComponent)) {
			ColliderComponent *collider = ecs_find(world, entity, ColliderComponent);

			json_map_begin(exporter, S("collider"));

			json_array_begin(exporter, S("center"));
			json_write_float3(exporter, collider->aabb.center);
			json_array_end(exporter);

			json_array_begin(exporter, S("extent"));
			json_write_float3(exporter, collider->aabb.extent);
			json_array_end(exporter);

			json_map_end(exporter);
		}

		json_map_end(exporter);

		if (ecs_has(world, entity, HierarchyComponent)) {
			HierarchyComponent *hierarchy = ecs_find(world, entity, HierarchyComponent);
			if (hierarchy->first_child) {
				json_array_begin(exporter, S("children"));
				Entity child = hierarchy->first_child;
				while (child) {
					serialize_entity(world, exporter, child);

					if (ecs_has(world, child, HierarchyComponent)) {
						HierarchyComponent *child_hierarchy = ecs_find(world, child, HierarchyComponent);
						child = child_hierarchy->next_sibling;
					} else
						child = 0;
				}
				json_array_end(exporter);
			}
		}
	}

	json_map_end(exporter);
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
