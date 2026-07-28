#pragma once

#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"

typedef struct {
	char name[64];
	int32_t parent;
} Bone;

typedef struct {
	float3 translation;
	quat4 rotation;
	float3 scale;
} Transform3;

typedef struct {
	char name[64];
	Transform3 **keyframes;
	float *timings;
	uint32_t keyframe_count, bone_count;
	float duration;
} AnimationClip;

typedef struct {
	AnimationClip *clips;
	uint32_t clip_count;

	uint32_t animation_index;
	float animation_t;
	bool loop;
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
