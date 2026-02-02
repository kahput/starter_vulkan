#pragma once

#include "common.h"
#include "core/cmath.h"

typedef enum {
	CAMERA_PROJECTION_PERSPECTIVE = 0,
	CAMERA_PROJECTION_ORTHOGRAPHIC
} CameraProjection;

typedef struct camera {
	Vector3f position, target, up;
	float fov;

	CameraProjection projection;
} Camera;

typedef struct material_paramters {
	Vector4f base_color_factor;
	float metallic_factor;
	float roughness_factor;
	Vector2f _pad0;
	Vector4f emissive_factor;
} MaterialParameters;

typedef enum {
	LIGHT_TYPE_DIRECTIONAL,
	LIGHT_TYPE_POINT,
} LightType;

typedef struct {
	LightType type;
	Vector3u _pad0;
	union {
		Vector4f position;
		Vector4f direction;
	} as;
	Vector4f color;
} Light;

#define MAX_POINT_LIGHTS 10
typedef struct {
	Matrix4f view;
	Matrix4f projection;
	Light directional_light;
	Light lights[MAX_POINT_LIGHTS];
	Vector3f camera_position;
	int light_count;
} FrameData;
