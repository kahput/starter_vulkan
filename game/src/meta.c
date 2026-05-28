#include "meta.h"

Type types[METATYPE_MAX] = {
	[METATYPE_Transform3] = {
	  .type = METATYPE_Transform3,
	  .name = "Transform3",
	  .alignment = alignof(Transform3),
	  .size = sizeof(Transform3),
	  .member_count = 4,
	  .members = (TypeMember[]){
		[0] = {
		  .type = METATYPE_float3,
		  .field_name = "position",
		  .offset = offsetof(Transform3, position),
		  .size = sizeof(((Transform3 *)0)->position),
		  .stride = sizeof(float3),
		  .count = 1,
		},
		[1] = {
		  .type = METATYPE_float3,
		  .field_name = "rotation",
		  .offset = offsetof(Transform3, rotation),
		  .size = sizeof(((Transform3 *)0)->rotation),
		  .stride = sizeof(float3),
		  .count = 1,
		},
		[2] = {
		  .type = METATYPE_float3,
		  .field_name = "scale",
		  .offset = offsetof(Transform3, scale),
		  .size = sizeof(((Transform3 *)0)->scale),
		  .stride = sizeof(float3),
		  .count = 1,
		},
		[3] = {
		  .type = METATYPE_float4x4,
		  .field_name = "world_matrix",
		  .offset = offsetof(Transform3, world_matrix),
		  .size = sizeof(((Transform3 *)0)->world_matrix),
		  .stride = sizeof(float4x4),
		  .count = 1,
		},
	  },
	},
	[METATYPE_MeshComponent] = {
	  .type = METATYPE_MeshComponent,
	  .name = "MeshComponent",
	  .alignment = alignof(MeshComponent),
	  .size = sizeof(MeshComponent),
	  .member_count = 2,
	  .members = (TypeMember[]){
		[0] = {
		  .type = METATYPE_UUID,
		  .field_name = "group_id",
		  .offset = offsetof(MeshComponent, group_id),
		  .size = sizeof(((MeshComponent *)0)->group_id),
		  .stride = sizeof(UUID),
		  .count = 1,
		},
		[1] = {
		  .type = METATYPE_uint32_t,
		  .field_name = "mesh_group_index",
		  .offset = offsetof(MeshComponent, mesh_group_index),
		  .size = sizeof(((MeshComponent *)0)->mesh_group_index),
		  .stride = sizeof(uint32_t),
		  .count = 1,
		},
	  },
	},
	/* [METATYPE_ColliderComponent] = { */
	  /* .type = METATYPE_ColliderComponent, */
	  /* .name = "ColliderComponent", */
	  /* .alignment = alignof(ColliderComponent), */
	  /* .size = sizeof(ColliderComponent), */
	  /* .member_count = 1, */
	  /* .members = (TypeMember[]){ */
		/* [0] = { */
		  /* .type = METATYPE_aabb, */
		  /* .field_name = "", */
		  /* .offset = offsetof(ColliderComponent, ), */
		  /* .size = sizeof(((ColliderComponent *)0)->), */
		  /* .stride = sizeof(aabb), */
		  /* .count = 1, */
		/* }, */
	  /* }, */
	/* }, */
	[METATYPE_HierarchyComponent] = {
	  .type = METATYPE_HierarchyComponent,
	  .name = "HierarchyComponent",
	  .alignment = alignof(HierarchyComponent),
	  .size = sizeof(HierarchyComponent),
	  .member_count = 4,
	  .members = (TypeMember[]){
		[0] = {
		  .type = METATYPE_Entity,
		  .field_name = "parent",
		  .offset = offsetof(HierarchyComponent, parent),
		  .size = sizeof(((HierarchyComponent *)0)->parent),
		  .stride = sizeof(Entity),
		  .count = 1,
		},
		[1] = {
		  .type = METATYPE_Entity,
		  .field_name = "first_child",
		  .offset = offsetof(HierarchyComponent, first_child),
		  .size = sizeof(((HierarchyComponent *)0)->first_child),
		  .stride = sizeof(Entity),
		  .count = 1,
		},
		[2] = {
		  .type = METATYPE_Entity,
		  .field_name = "next_sibling",
		  .offset = offsetof(HierarchyComponent, next_sibling),
		  .size = sizeof(((HierarchyComponent *)0)->next_sibling),
		  .stride = sizeof(Entity),
		  .count = 1,
		},
		[3] = {
		  .type = METATYPE_Entity,
		  .field_name = "prev_sibling",
		  .offset = offsetof(HierarchyComponent, prev_sibling),
		  .size = sizeof(((HierarchyComponent *)0)->prev_sibling),
		  .stride = sizeof(Entity),
		  .count = 1,
		},
	  },
	},
	[METATYPE_InventorySlot] = {
	  .type = METATYPE_InventorySlot,
	  .name = "InventorySlot",
	  .alignment = alignof(InventorySlot),
	  .size = sizeof(InventorySlot),
	  .member_count = 2,
	  .members = (TypeMember[]){
		[0] = {
		  .type = METATYPE_uint32_t,
		  .field_name = "item_index",
		  .offset = offsetof(InventorySlot, item_index),
		  .size = sizeof(((InventorySlot *)0)->item_index),
		  .stride = sizeof(uint32_t),
		  .count = 1,
		},
		[1] = {
		  .type = METATYPE_uint8_t,
		  .field_name = "quantity",
		  .offset = offsetof(InventorySlot, quantity),
		  .size = sizeof(((InventorySlot *)0)->quantity),
		  .stride = sizeof(uint8_t),
		  .count = 1,
		},
	  },
	},
	[METATYPE_InventoryComponent] = {
	  .type = METATYPE_InventoryComponent,
	  .name = "InventoryComponent",
	  .alignment = alignof(InventoryComponent),
	  .size = sizeof(InventoryComponent),
	  .member_count = 3,
	  .members = (TypeMember[]){
		[0] = {
		  .type = METATYPE_InventorySlot,
		  .field_name = "slots",
		  .offset = offsetof(InventoryComponent, slots),
		  .size = sizeof(((InventoryComponent *)0)->slots),
		  .stride = sizeof(InventorySlot),
		  .count = MAX_INVENTORY_SLOTS,
		},
		[1] = {
		  .type = METATYPE_InventorySlot,
		  .field_name = "active_item",
		  .offset = offsetof(InventoryComponent, active_item),
		  .size = sizeof(((InventoryComponent *)0)->active_item),
		  .stride = sizeof(InventorySlot),
		  .count = 1,
		},
		[2] = {
		  .type = METATYPE_uint32_t,
		  .field_name = "capacity",
		  .offset = offsetof(InventoryComponent, capacity),
		  .size = sizeof(((InventoryComponent *)0)->capacity),
		  .stride = sizeof(uint32_t),
		  .count = 1,
		},
	  },
	},
};
