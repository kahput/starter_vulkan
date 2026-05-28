#include "components.h"

typedef enum {
	METATYPE_Transform3,
	METATYPE_MeshComponent,
	METATYPE_ColliderComponent,
	METATYPE_HierarchyComponent,
	METATYPE_InventorySlot,
	METATYPE_InventoryComponent,

	METATYPE_UUID,
	METATYPE_float3,
	METATYPE_float4x4,
	METATYPE_Entity,

	METATYPE_uint32_t,
	METATYPE_uint8_t,

	METATYPE_MAX,
} TypeIdentifier;

enum {
	METATYPE_MEMBER_FLAG_POINTER = 1 << 0,
	METATYPE_MEMBER_FLAG_CONST = 1 << 1,
	METATYPE_MEMBER_FLAG_ARRAY= 1 << 2,
};

typedef struct {
	TypeIdentifier type;
	const char *field_name;

	bool is_pointer;
	uint64_t offset, size;
	uint64_t count, stride;
} TypeMember;

typedef struct type Type;
struct type {
	TypeIdentifier type;
	const char *name;

	uint64_t size, alignment;

	TypeMember *members;
	uint32_t member_count;
};

#define SCALAR_MEMBER(T, field)        \
	.size = sizeof(((T *)0)->field),   \
	.stride = sizeof(((T *)0)->field), \
	.count = 1

extern Type types[METATYPE_MAX];
