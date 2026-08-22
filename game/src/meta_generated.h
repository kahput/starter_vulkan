#pragma once

#include "meta.h"

enum {
    TYPE_float,
    TYPE_float32x2,
    TYPE_float32x3,
    TYPE_float32x4,
    TYPE_double,
    TYPE_float64x2,
    TYPE_float64x3,
    TYPE_float64x4,
    TYPE_uint32_t,
    TYPE_uint32x2,
    TYPE_uint32x3,
    TYPE_uint32x4,
    TYPE_uint64_t,
    TYPE_uint64x2,
    TYPE_uint64x3,
    TYPE_uint64x4,
    TYPE_int32_t,
    TYPE_int32x2,
    TYPE_int32x3,
    TYPE_int32x4,
    TYPE_Interval,
    TYPE_Interval2,
    TYPE_Interval3,
    TYPE_uint8_t,
    TYPE_Color,
    TYPE_size_t,
    TYPE_Buffer,
    TYPE_float4x4,
    TYPE_Ray3,
    TYPE_bool,
    TYPE_Raycast3Result,
    TYPE_Rectangle,
    TYPE_Transform3,
    TYPE_UUID,
    TYPE_MeshComponent,
    TYPE_ColliderComponent,
    TYPE_HierarchyComponent,
    TYPE_InventorySlot,
    TYPE_InventoryComponent,

    TYPE_MAX,
};

#define TYPE_float64 TYPE_double
#define TYPE_double2 TYPE_float64x2
#define TYPE_double3 TYPE_float64x3
#define TYPE_double4 TYPE_float64x4
#define TYPE_uint32 TYPE_uint32_t
#define TYPE_uint64 TYPE_uint64_t
#define TYPE_uint2 TYPE_uint32x2
#define TYPE_uint3 TYPE_uint32x3
#define TYPE_uint4 TYPE_uint32x4
#define TYPE_int32 TYPE_int32_t
#define TYPE_int2 TYPE_int32x2
#define TYPE_int3 TYPE_int32x3
#define TYPE_int4 TYPE_int32x4
#define TYPE_Flag TYPE_uint32_t
#define TYPE_float2 TYPE_float32x2
#define TYPE_float3 TYPE_float32x3
#define TYPE_float4 TYPE_float32x4
#define TYPE_Entity TYPE_uint64_t
#define TYPE_TransformComponent TYPE_Transform3

extern Type type_introspection[TYPE_MAX];

#define type_info(T) type_introspection[TYPE_##T]