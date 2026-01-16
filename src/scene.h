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

typedef struct material_paramters {
	vec4 base_color_factor;
	float metallic_factor;
	float roughness_factor;
	vec2 _pad0;
	vec4 emissive_factor;
} MaterialParameters;

typedef enum {
	LIGHT_TYPE_DIRECTIONAL,
	LIGHT_TYPE_POINT,
} LightType;

typedef struct {
	LightType type;
	ivec3 _pad0;
	union {
		vec4 position;
		vec4 direction;
	} as;
	vec4 color;
} Light;

#define MAX_POINT_LIGHTS 10
typedef struct {
	mat4 view;
	mat4 projection;
	Light directional_light;
	Light lights[MAX_POINT_LIGHTS];
	vec3 camera_position;
	int light_count;
} FrameData;
