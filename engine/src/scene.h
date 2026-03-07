#pragma once

#include "common.h"
#include "core/cmath.h"

typedef enum {
	CAMERA_PROJECTION_PERSPECTIVE,
	CAMERA_PROJECTION_ORTHOGRAPHIC
} CameraProjection;

typedef struct camera {
	float32_3 position, target, up;
	float fov;

	CameraProjection projection;
} Camera;

typedef struct material_paramters {
	float32_4 base_color_factor;
	float metallic_factor;
	float roughness_factor;
	float32_2 _pad0;
	float32_4 emissive_factor;
} MaterialParameters;

typedef enum {
	LIGHT_TYPE_DIRECTIONAL,
	LIGHT_TYPE_POINT,
} LightType;

typedef struct {
	LightType type;
	uint32_3 _pad0;
	union {
		float32_4 position;
		float32_4 direction;
	} as;
	float32_4 color;
} Light;

#define MAX_POINT_LIGHTS 10
typedef struct {
	Matrix4f view;
	Matrix4f projection;
	Light directional_light;
	Light lights[MAX_POINT_LIGHTS];
	float32_3 camera_position;
	int light_count;
} FrameData;
