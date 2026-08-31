#pragma once

#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/geom_types.h"

typedef struct {
	char name[64];
	int32_t parent;
} Bone;

typedef struct {
	char name[64];
	Transform3 **keyframes;
	float *timings;
	uint32_t keyframe_count, bone_count;
	float duration;
} AnimationClip;

typedef struct {
	bool playing;

	AnimationClip animation;
	float anim_t;
} AnimationPlayer;

typedef struct {
	Transform3 *transforms;
	uint32_t bone_count;
} Pose;

typedef struct {
	float *weights;
	uint32_t bone_count;
} BoneMask;
typedef struct {
	Bone *bones;
	uint32_t bone_count;

	float4x4 *inverse_rest_matrices;
	float4x4 *bind_pose_matrices;
} Skeleton;

Pose anim_pose_sample(Arena *arena, AnimationClip *clip, float t);
Pose anim_pose_blend_local(Arena *arena, Pose *dst, Pose *src, float blend_weight, BoneMask *mask);
float4x4 *anim_pose_local_to_model(Arena *arena, Pose *pose, Skeleton *skeleton);
float4x4 *anim_pose_skinning_matrices(Arena *arena, float4x4 *model_matrices, Skeleton *skeleton);

void anim_player_update(AnimationPlayer *player, float dt);

typedef struct {
	float start, target;
	float elapsed, duration;
} ANIM_Tween1f;

INLINE float tween1f_eval(ANIM_Tween1f *tw) {
	float u = tw->duration > 0.0f ? clampf(tw->elapsed / tw->duration, 0.0f, 1.0f) : 1.0f;
	return lerpf(tw->start, tw->target, u);
}

INLINE float tween1f_update(ANIM_Tween1f *tw, float target, float duration, float dt) {
	if (tw->target != target) {
		tw->start = tween1f_eval(tw);
		tw->target = target;
		tw->elapsed = 0.0f;
		tw->duration = duration;
	}
	tw->elapsed += dt;
	return tween1f_eval(tw);
}
