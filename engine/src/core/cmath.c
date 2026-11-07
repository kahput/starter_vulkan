#include "cmath.h"
#include "core/logger.h"
#include <stdlib.h>

float randf(void) { return (float)rand() / RAND_MAX; }
int32_t randi(void) { return rand(); }

quat4 slerp4q(quat4 q, quat4 p, float t) {
	quat4 result = { 0 };

	float cosHalfTheta = q.x * p.x + q.y * p.y + q.z * p.z + q.w * p.w;

	if (cosHalfTheta < 0) {
		p.x = -p.x;
		p.y = -p.y;
		p.z = -p.z;
		p.w = -p.w;
		cosHalfTheta = -cosHalfTheta;
	}

	if (fabsf(cosHalfTheta) >= 1.0f)
		result = q;
	else {
		float halfTheta = acosf(cosHalfTheta);
		float sinHalfTheta = sqrtf(1.0f - cosHalfTheta * cosHalfTheta);

		if (fabsf(sinHalfTheta) < EPSILON) {
			result.x = q.x * (1.0f - t) + p.x * t;
			result.y = q.y * (1.0f - t) + p.y * t;
			result.z = q.z * (1.0f - t) + p.z * t;
			result.w = q.w * (1.0f - t) + p.w * t;
		} else {
			float ratioA = sinf((1 - t) * halfTheta) / sinHalfTheta;
			float ratioB = sinf(t * halfTheta) / sinHalfTheta;

			result.x = (q.x * ratioA + p.x * ratioB);
			result.y = (q.y * ratioA + p.y * ratioB);
			result.z = (q.z * ratioA + p.z * ratioB);
			result.w = (q.w * ratioA + p.w * ratioB);
		}
	}

	return result;
}

float4x4 orthographic(float left, float right, float bottom, float top, float near, float far) {
	float4x4 result = identity4x4();

	result.elements[0] = 2.0f / (right - left);
	result.elements[5] = 2.0f / (top - bottom);
	result.elements[10] = -1.0f / (far - near);

	result.elements[12] = -(right + left) / (right - left);
	result.elements[13] = -(top + bottom) / (top - bottom);
	result.elements[14] = -(near) / (far - near);
	result.elements[15] = 1.0f;

	return result;
}
