#pragma once

#include "common.h"
#include "core/cmath.h"
#include "core/strings.h"

typedef struct {
	float2 a, b, c;
} Triangle2;

typedef struct {
	float2 origin, direction;
} Ray2;

// :2d

typedef struct {
	float3 translation;
	quat4 rotation;
	float3 scale;
} Transform3;
INLINE Transform3 transform3(float3 translation, quat4 rotation, float3 scale) { return (Transform3){ translation, rotation, scale }; }
INLINE float4x4 transform4x4(Transform3 transform) { return affine4x4(mul3x3(quat3x3(transform.rotation), diagonal3x3(transform.scale)), transform.translation); }

typedef struct {
	bool hit;
	float t;
	float3 point, normal;
} CastResult3;
static CastResult3 CAST3_NO_HIT = { false, INFINITY, { 0, 0, 0 }, { 0, 0, 0 } };

// Direction must be normalized for helper functions
typedef struct {
	float3 origin, direction;
} Ray3;

typedef struct {
	float3 a, b;
} Segment3;
INLINE Segment3 seg3(float3 a, float3 b) { return (Segment3){ a, b }; }

typedef struct {
	float3 a, b, c;
} Triangle3;

typedef struct {
	float3 min, max;
} AABB3;

typedef struct {
	float3 center;
	float radius;
} Sphere;

typedef struct {
	float3 normal;
	float distance;
} Plane;

typedef struct {
	float3 a, b;
	float radius;
} Capsule3;

typedef struct {
	float3 *vertices;
	uint32_t vertex_count;
} Convex3;

typedef struct {
	float2 *vertices;
	uint32_t vertex_count;
} Convex2;

typedef enum {
	SHAPE_KIND_AABB3,
	SHAPE_KIND_SPHERE,
	SHAPE_KIND_CAPSULE3,
	SHAPE_KIND_PLANE,
	SHAPE_KIND_CONVEX_POLYGON,
	/* SHAPE_KIND_CYLINDER, */
	/* SHAPE_KIND_CONCAVE_POLYGON, */
	/* SHAPE_KIND_HEIGHTMAP, */

	SHAPE_KIND_MAX,
} ShapeKind;
static string8 shape_kind_to_string[SHAPE_KIND_MAX] = {
	ENUM_STRING_TABLE_ENTRY(SHAPE_KIND, AABB3),
	ENUM_STRING_TABLE_ENTRY(SHAPE_KIND, SPHERE),
	ENUM_STRING_TABLE_ENTRY(SHAPE_KIND, CAPSULE3),
	ENUM_STRING_TABLE_ENTRY(SHAPE_KIND, PLANE),
	ENUM_STRING_TABLE_ENTRY(SHAPE_KIND, CONVEX_POLYGON),
};

typedef struct {
	ShapeKind kind;

	union {
		AABB3 aabb3;
		Sphere sphere;
		Capsule3 capsule;
		Plane plane;
		Convex3 convex;
	} as;
} Shape3;

// :3d
