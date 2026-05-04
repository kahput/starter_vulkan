#include "core/cmath.h"

typedef uint64_t Entity;

typedef struct {
	float3 position;
	float3 rotation;
	float3 scale;

	float4x4 world_matrix;
} Transform3;
typedef Transform3 TransformComponent;

typedef struct {
	uint32_t mesh_group_index;
} MeshComponent;

typedef union {
	struct {
		float3 center;
		float3 extent;
	} aabb;
} ColliderComponent;

typedef struct {
	Entity parent;
	Entity first_child;
	Entity next_sibling;
	Entity prev_sibling;
} HierarchyComponent;
