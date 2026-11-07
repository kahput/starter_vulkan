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

Ray3 camera_mouse_ray(Camera *camera, Rectangle viewport, float2 mouse) {
	float2 viewport_half_extent = rect_half_extent(viewport);
	float2 mouse_ndc = {
		(mouse.x - viewport_half_extent.x) / viewport_half_extent.x,
		(mouse.y - viewport_half_extent.y) / viewport_half_extent.y,
	};
	float halfh = tanf(radians(camera->fovy) * 0.5f);
	float halfw = (viewport.width / viewport.height) * halfh;
	float3 mouse_view = { mouse_ndc.x * halfw, mouse_ndc.y * -halfh, -1.0f }; // before perspective divide

	float3 forward = normalize3(sub3(camera->target, camera->position));
	float3 right = normalize3(cross3(forward, FLOAT3_UP));
	float3 up = cross3(right, forward);

	float4x4 world_from_view = affine4x4(columns3x3(right, up, neg3(forward)), camera->position);
	float3 mouse_world = xform3p(world_from_view, mouse_view);

	return (Ray3){
		camera->position,
		normalize3(sub3(mouse_world, camera->position)),
	};
}
