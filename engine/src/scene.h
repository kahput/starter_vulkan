#pragma once

#include "common.h"
#include "core/cmath.h"

typedef enum {
	CAMERA_PROJECTION_PERSPECTIVE,
	CAMERA_PROJECTION_ORTHOGRAPHIC
} CameraProjection;

typedef struct camera {
	float3 position, target, up;
	float fov;

	CameraProjection projection;
} Camera;

typedef struct alignas(16) material_paramters {
	float4 base_color_factor;
	float3 emissive_factor;
	float metallic_factor;
	float roughness_factor;
} MaterialParameters;

typedef enum {
	LIGHT_TYPE_DIRECTIONAL,
	LIGHT_TYPE_POINT,
} LightType;

typedef struct {
	LightType type;
	uint32x3 _pad0;
	union {
		float32x4 position;
		float32x4 direction;
	} as;
	float32x4 color;
} Light;

#define MAX_POINT_LIGHTS 10
typedef struct {
	float4x4 view;
	float4x4 projection;
	Light directional_light;
	Light lights[MAX_POINT_LIGHTS];
	float32x3 camera_position;
	int light_count;
} FrameData;
