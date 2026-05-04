#include "common.h"
#include "core/arena.h"

#include "components.h"
#include "core/strings.h"

#define MAX_ENTITIES 1024

typedef enum {
	COMPONENT_TYPE_NILL,

	COMPONENT_TYPE_Transform3,
	COMPONENT_TYPE_TransformComponent = COMPONENT_TYPE_Transform3,
	COMPONENT_TYPE_HierarchyComponent,

	COMPONENT_TYPE_MeshComponent,
	COMPONENT_TYPE_ColliderComponent,
	// :component

	COMPONENT_TYPE_MAX,
} ComponentID;
STATIC_ASSERT(COMPONENT_TYPE_MAX < 32);

typedef struct {
	size_t element_size, element_align;
} ComponentMetadata;

// TODO: Introspect this
static ComponentMetadata component_metadata[COMPONENT_TYPE_MAX] = {
	[COMPONENT_TYPE_TransformComponent] = { sizeof(TransformComponent), alignof(TransformComponent) },
	[COMPONENT_TYPE_MeshComponent] = { sizeof(MeshComponent), alignof(MeshComponent) },
	[COMPONENT_TYPE_ColliderComponent] = { sizeof(ColliderComponent), alignof(ColliderComponent) },
	[COMPONENT_TYPE_HierarchyComponent] = { sizeof(HierarchyComponent), alignof(HierarchyComponent) },
	// :component
};

typedef struct ECS ECS;
ECS *ecs_make(Arena *arena);

bool ecs_valid(ECS *world, Entity entity);

Entity ecs_spawn(ECS *world, float3 position);
Entity ecs_copy(ECS *world, Entity target);
void ecs_despawn(ECS *world, Entity entity);

bool ecs_has_id(ECS *world, Entity entity, ComponentID type_id);
void *ecs_push_id(ECS *world, Entity entity, ComponentID type_id);
void *ecs_find_id(ECS *world, Entity entity, ComponentID type_id);
void ecs_pop_id(ECS *world, Entity entity, ComponentID type_id);

// TODO: Component specific APIs, maybe move these out?
void ecs_hierarchy_parent(ECS *world, Entity parent, Entity target);
void ecs_hierarchy_unparent(ECS *world, Entity entity);

void ecs_hierarchical_despawn(ECS *world, Entity root);
Entity ecs_hierarchical_copy(ECS *world, Entity root);

void ecs_serialize_entity(ECS *world, Entity root, String output_path);
Entity ecs_deserialize_entity(ECS *world, String path);

#define ecs_type_id(T) COMPONENT_TYPE_##T

#define ecs_has(world, entity, T) (ecs_has_id((world), (entity), ecs_type_id(T)))
#define ecs_push(world, entity, T) ((T *)ecs_push_id((world), (entity), ecs_type_id(T)))
#define ecs_pop(world, entity, T) ecs_pop_id((world), (entity), ecs_type_id(T))
#define ecs_find(world, entity, T) ((T *)ecs_find_id((world), (entity), ecs_type_id(T)))
#define ecs_put(world, entity, T, ...)      \
	do {                                    \
		T _val = __VA_ARGS__;               \
		*ecs_push(world, entity, T) = _val; \
	} while (0)

typedef struct {
	ECS *world;
	uint32_t mask;
	Entity current;
} EcsIterator;

EcsIterator ecs_query_make(ECS *world, uint32_t count, ComponentID *component_ids);
#define ecs_query(world, ...) \
	ecs_query_make((world), sizeof((ComponentID[]){ __VA_ARGS__ }) / sizeof(ComponentID), (ComponentID[]){ __VA_ARGS__ })

Entity ecs_next(EcsIterator *it);
