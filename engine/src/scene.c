#include "scene.h"
#include "core/cmath.h"
#include "input.h"

static float turn_rate = 2.0f;
static float pan_speed = 5.0f;
static float zoom_speed = 5.0f;

void scene_camera_orbit(Camera3 *camera, float2 mouse_delta) {
	float3 camera_target_offset = float3_subtract(camera->target, camera->position);

	float3 camera_forward = float3_normalize(camera_target_offset);
	float3 camera_right = float3_cross(camera->up, camera_forward);
	float3 camera_up = float3_cross(camera_right, camera_forward);

	float yaw_delta = mouse_delta.x * turn_rate;
	float pitch_delta = mouse_delta.y * turn_rate;
	/*
	 * x = RADIUS * cos(azimuth) * sin(theta) + offset.x;
	 * y = RADIUS * cos(theta) + offset.y
	 * z = RADIUS * sin(azimuth) * sin(theta) + offset.z;
	 */

	if (input_mouse_down(MOUSE_BUTTON_MIDDLE) && input_key_down(KEY_CODE_LEFTSHIFT)) {
		float2 shift = float2_scale(mouse_delta, pan_speed);
		shift.y *= -1;

		camera->position = float3_add(camera->position, float3_scale(camera_right, shift.x));
		camera->position = float3_add(camera->position, float3_scale(camera_up, shift.y));

		camera->target = float3_add(camera->position, camera_target_offset);

	} else if (input_mouse_down(MOUSE_BUTTON_MIDDLE) && input_key_down(KEY_CODE_LEFTCTRL)) {
		float zoom = mouse_delta.y * zoom_speed;
		float3 camera_forward = float3_normalize(float3_subtract(camera->target, camera->position));
		camera->position = float3_add(camera->position, float3_scale(camera_forward, -zoom));
	} else if (input_mouse_down(MOUSE_BUTTON_MIDDLE)) {
		float yaw_delta = mouse_delta.x * turn_rate;
		float pitch_delta = mouse_delta.y * turn_rate;
		/*
		 * x = RADIUS * cos(azimuth) * sin(theta) + offset.x;
		 * y = RADIUS * cos(theta) + offset.y
		 * z = RADIUS * sin(azimuth) * sin(theta) + offset.z;
		 */

		float3 camera_position = float3_subtract(camera->position, camera->target);
		float r = MAX(float3_length(camera_position), EPSILON);

		float camera_xz = float2_length((float2){ camera_position.x, camera_position.z });
		float current_theta = atan2f(camera_xz, camera_position.y);
		// tan(theta) = o / a = y / x;
		float current_azimuth = atan2f(camera_position.z, camera_position.x);

		current_theta = CLAMP(current_theta - pitch_delta, EPSILON * 2, C_PIf - EPSILON * 2);
		current_azimuth += yaw_delta;

		// Apply move
		camera->position = float3_add(
			(float3){
			  .x = r * sinf(current_theta) * cosf(current_azimuth),
			  .y = r * cosf(current_theta),
			  .z = r * sinf(current_theta) * sinf(current_azimuth),
			},
			camera->target);
	}
}
