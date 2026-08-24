#include "camera.h"
#include "common.h"
#include "core/cmath.h"

float4x4 camera_view(Camera *camera) {
	return lookat(camera->position, camera->target, camera->up);
}

float4x4 camera_proj(Camera *camera, float aspect) {
	float4x4 result = identity4x4();

	switch (camera->projection) {
		case CAMERA_PROJECTION_PERSPECTIVE: {
			result = perspective(camera->fovy * DEG2RAD, aspect, camera->near, camera->far);
		} break;
		case CAMERA_PROJECTION_ORTHOGRAHPIC: {
			float half_h = camera->fovy * 0.5f;
			float half_w = half_h * aspect;

			result = orthographic(-half_w, half_w, -half_h, half_h, camera->near, camera->far);
		} break;
	}

	return result;
}

float4x4 camera_view_proj(Camera *camera, float aspect) {
	return mul4x4(camera_proj(camera, aspect), camera_view(camera));
}
