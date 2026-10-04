#pragma once

#include "common.h"
#include "core/cmath.h"

typedef enum {
	CAMERA_PROJECTION_PERSPECTIVE,
	CAMERA_PROJECTION_ORTHOGRAHPIC,
} CameraProjeciton;

typedef struct {
	CameraProjeciton projection;
	float3 position, target, up;
	float fovy, near, far;
} Camera;

float4x4 camera_view(Camera *camera);
float4x4 camera_proj(Camera *camera, float aspect);
float4x4 camera_view_proj(Camera *camera, float aspect);
