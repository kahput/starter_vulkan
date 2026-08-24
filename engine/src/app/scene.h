#pragma once

#include "common.h"
#include "core/cmath.h"

#include "draw/camera.h"

void scene_camera_orbit(Camera *camera, float2 mouse_delta);
void scene_camera_follow(Camera *camera, float2 mouse_delta, float3 target);
