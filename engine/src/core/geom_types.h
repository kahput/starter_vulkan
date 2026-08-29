#pragma once

#include "common.h"
#include "core/cmath.h"
#include "core/strings.h"

typedef struct {
	float2 a, b, c;
} Triangle2;
// :2d

typedef struct {
	bool hit;
	float t;
	float3 normal, point;
} CastResult3;
static CastResult3 CAST3_NO_HIT = { false, INFINITY, { 0, 0, 0 }, { 0, 0, 0 } };

typedef struct {
	float3 origin, direction;
} Ray3;

typedef struct {
	float3 a, b;
} Segment3;

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
} ConvexPolyhedron;

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
static String8 shape_kind_to_string[SHAPE_KIND_MAX] = {
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
		ConvexPolyhedron convex;
	} as;
} Shape3;

// :3d
