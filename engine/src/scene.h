#pragma once

#include "common.h"
#include "core/cmath.h"

typedef enum {
	CAMERA_PROJECTION_PERSPECTIVE = 0,
	CAMERA_PROJECTION_ORTHOGRAPHIC
} CameraProjection;

typedef struct camera {
	float3 position, target, up;
	float fov;

	CameraProjection projection;
} Camera;

typedef struct material_paramters {
	float4 base_color_factor;
	float metallic_factor;
	float roughness_factor;
	float2 _pad0;
	float4 emissive_factor;
} MaterialParameters;

typedef enum {
	LIGHT_TYPE_DIRECTIONAL,
	LIGHT_TYPE_POINT,
} LightType;

typedef struct {
	LightType type;
	uint3 _pad0;
	union {
		float4 position;
		float4 direction;
	} as;
	float4 color;
} Light;

#define MAX_POINT_LIGHTS 10
typedef struct {
	Matrix4f view;
	Matrix4f projection;
	Light directional_light;
	Light lights[MAX_POINT_LIGHTS];
	float3 camera_position;
	int light_count;
} FrameData;
