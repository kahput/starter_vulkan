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
	float fovy;
} Camera3;

void scene_camera_orbit(Camera3 *camera, float2 mouse_delta);
