#pragma once
#include <cglm/cglm.h>

typedef enum {
	CAMERA_PROJECTION_PERSPECTIVE = 0,
	CAMERA_PROJECTION_ORTHOGRAPHIC
} CameraProjection;

typedef struct camera {
	vec3 position, target, up;
	float fov;

	CameraProjection projection;
} Camera;
