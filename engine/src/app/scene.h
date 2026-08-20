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
} Camera3D;

void scene_camera_orbit(Camera3D *camera, float2 mouse_delta);
void scene_camera_follow(Camera3D *camera, float2 mouse_delta, float3 target);
