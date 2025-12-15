#include "camera.h"

#include <cglm/cglm.h>

typedef struct _camera {
	vec3 position, target, up;
	float fov;

	// CameraProjection projection;
} _Camera;
