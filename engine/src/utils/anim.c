#include "anim.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"
#include <math.h>

Pose anim_pose_sample(Arena *arena, AnimationClip *clip, float t) {
	Pose result = { 0 };

	bool ok = clip && clip->timings && clip->keyframes;

	int32_t target_frame = -1;
	if (ok) {
		result.transforms = t > clip->timings[clip->keyframe_count - 1] ? clip->keyframes[clip->keyframe_count - 1] : clip->keyframes[0];
		result.bone_count = clip->bone_count;

		for (uint32_t keyframe = 0; keyframe < clip->keyframe_count; ++keyframe) {
			if (clip->timings[keyframe] > t) {
				target_frame = keyframe;
				break;
			}
		}

		ok = target_frame != -1 && target_frame > 0;
	}

	if (ok) {
		Pose source = { .transforms = clip->keyframes[target_frame - 1], .bone_count = clip->bone_count };
		Pose target = { .transforms = clip->keyframes[target_frame], .bone_count = clip->bone_count };

		float t0 = clip->timings[target_frame - 1] / clip->duration;
		float t1 = clip->timings[target_frame] / clip->duration;

		result = anim_pose_blend_local(arena, &source, &target, (t / clip->duration - t0) / (t1 - t0), NULL);
	}

	return result;
}

static inline float anim__bone_blend_weight(float blend_weight, BoneMask *mask, uint32_t bone_index) {
	if (mask) {
		return blend_weight * mask->weights[bone_index];
	} else {
		return blend_weight;
	}
}

Pose anim_pose_blend_local(Arena *arena, Pose *source, Pose *target, float blend_weight, BoneMask *mask) {
	Pose result = { 0 };
	ASSERT(arena && target && source);
	ASSERT(blend_weight >= 0.0f && blend_weight <= 1.0f);
	ASSERT(target->bone_count == source->bone_count);

	result.bone_count = target->bone_count;
	result.transforms = arena_push_count(arena, Transform3, result.bone_count);

	for (uint32_t bone_index = 0; bone_index < target->bone_count; ++bone_index) {
		float bone_blend_weight = anim__bone_blend_weight(blend_weight, mask, bone_index);
		ASSERT(bone_blend_weight >= 0.0f && bone_blend_weight <= 1.0f);
		if (bone_blend_weight == 0.0f) {
			result.transforms[bone_index] = source->transforms[bone_index];
		} else {
			Transform3 *source_transform = &source->transforms[bone_index];
			Transform3 *target_transform = &target->transforms[bone_index];

			result.transforms[bone_index].translation = lerp3(
				source_transform->translation,
				target_transform->translation,
				bone_blend_weight);

			result.transforms[bone_index].rotation = quat4_slerp(
				source_transform->rotation,
				target_transform->rotation,
				bone_blend_weight);

			result.transforms[bone_index].scale = lerp3(
				source_transform->scale,
				target_transform->scale,
				bone_blend_weight);
		}
	}

	return result;
}

float4x4 *anim_pose_local_to_model(Arena *arena, Pose *pose, Skeleton *skeleton) {
	float4x4 *result = arena_push_count(arena, float4x4, skeleton->bone_count);

	for (uint32_t bone_index = 0; bone_index < skeleton->bone_count; ++bone_index) {
		float4x4 local = compose4x4_quat(
			pose->transforms[bone_index].translation,
			pose->transforms[bone_index].rotation,
			pose->transforms[bone_index].scale);

		if (skeleton->bones[bone_index].parent == -1)
			result[bone_index] = local;
		else
			result[bone_index] = mul4x4(result[skeleton->bones[bone_index].parent], local);
	}

	return result;
}

float4x4 *anim_pose_skinning_matrices(Arena *arena, float4x4 *model_matrices, Skeleton *skeleton) {
	float4x4 *result = arena_push_count(arena, float4x4, skeleton->bone_count);

	for (uint32_t bone_index = 0; bone_index < skeleton->bone_count; ++bone_index)
		result[bone_index] = mul4x4(model_matrices[bone_index], skeleton->inverse_rest_matrices[bone_index]);

	return result;
}
