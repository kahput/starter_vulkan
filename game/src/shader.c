#include "assets/asset_types.h"
#include "assets/importer.h"
#include "common.h"
#include "core/arena.h"
#include "core/debug.h"
#include "core/strings.h"
#include "renderer/backend/vulkan_api.h"
#include <core/r_types.h>

/* #define SP_FLOAT(n, v) ((ShaderParameter){ .name = S(n), .type = SHADER_PARAMETER_FLOAT, .as.f = (v) }) */
/* #define SP_FLOAT2(n, ...) ((ShaderParameter){ .name = S(n), .type = SHADER_PARAMETER_FLOAT2, .as.f2 = { __VA_ARGS__ } }) */
/* #define SP_FLOAT3(n, ...) ((ShaderParameter){ .name = S(n), .type = SHADER_PARAMETER_FLOAT3, .as.f3 = { __VA_ARGS__ } }) */
/* #define SP_FLOAT4(n, ...) ((ShaderParameter){ .name = S(n), .type = SHADER_PARAMETER_FLOAT4, .as.f4 = { __VA_ARGS__ } }) */
/* #define SP_TEXTURE(n, ...) ((ShaderParameter){ .name = S(n), .type = SHADER_PARAMETER_TEXTURE, .as.texture = __VA_ARGS__ }) */
/* #define SP_BUFFER(n, ...) ((ShaderParameter){ .name = S(n), .type = SHADER_PARAMETER_RAW, .as.buffer = { __VA_ARGS__}) */
/* static inline uint32_t _shader_find_offset(String name, uint32_t set, ShaderReflection *metadata); */
/* static inline uint32_t _shader_find_texture_index(String name, uint32_t set, ShaderReflection *metadata); */
/* static inline uint32_t _shader_parameter_to_stride(ShaderParameter *param); */
/* static inline bool _shader_parameter_set(ShaderReflection *metadata, ShaderParameter *param, uint8_t *buffer, Texture2D *textures); */

/* Shader shader_make_with_defaults(Arena *arena, VulkanContext *context, String name, String vertex, String fragment, ShaderParameter *defaults) { */
/* 	Shader result = { 0 }; */

/* 	ArenaTemp scratch = arena_scratch_begin(arena); */
/* 	ShaderSource shader_src = importer_load_shader(scratch.arena, vertex, fragment); */

/* 	result.name = string_copy(arena, name); */
/* 	result.handle = vulkan_shader_make(arena, context, name, shader_src.vertex, shader_src.fragment, &result.metadata); */

/* 	for (uint32_t index = 0; index < SHADER_UNIFORM_FREQUENCY_MAX; ++index) { */
/* 		result.defaults[index].buffer_size = result.metadata.sets[index].total_buffer_size; */
/* 		result.defaults[index].buffer = arena_push_size(arena, result.defaults[index].buffer_size); */

/* 		result.defaults[index].texture_count = result.metadata.sets[index].total_texture_count; */
/* 		result.defaults[index].textures = arena_push_count(arena, result.defaults[index].texture_count, Texture2D); */
/* 	} */

/* 	uint32_t index = 0; */
/* 	while (defaults[index].type) { */
/* 		ShaderParameter *param = &defaults[index++]; */

/* 		if (_shader_parameter_set(&result.metadata, */
/* 				param, */
/* 				result.defaults[SHADER_UNIFORM_FREQUENCY_PER_MATERIAL].buffer, */
/* 				result.defaults[SHADER_UNIFORM_FREQUENCY_PER_MATERIAL].textures) == false) { */
/* 			ASSERT_FORMAT(false, "shader[%.*s] invalid uniform passed (%.*s)", sarg(name), sarg(param->name)); */
/* 		} */
/* 	} */

/* 	arena_scratch_end(scratch); */

/* 	return result; */
/* } */

/* ShaderMaterial shader_material_make_with_parameters(Arena *arena, Shader *shader, ShaderParameter *params) { */
/* 	ShaderMaterial result = { .shader = shader }; */

/* 	result.parameters.buffer_size = result.shader->metadata.sets[SHADER_UNIFORM_FREQUENCY_PER_MATERIAL].total_buffer_size; */
/* 	result.parameters.buffer = (uint8_t *)arena_push_copy( */
/* 		arena, */
/* 		result.shader->defaults[SHADER_UNIFORM_FREQUENCY_PER_MATERIAL].buffer, */
/* 		result.parameters.buffer_size); */

/* 	result.parameters.texture_count = result.shader->metadata.sets[SHADER_UNIFORM_FREQUENCY_PER_MATERIAL].total_texture_count; */
/* 	result.parameters.textures = (Texture2D *)arena_push_copy( */
/* 		arena, */
/* 		result.shader->defaults[SHADER_UNIFORM_FREQUENCY_PER_MATERIAL].textures, */
/* 		result.parameters.texture_count); */

/* 	uint32_t index = 0; */
/* 	while (params && params[index].type) { */
/* 		ShaderParameter *param = &params[index++]; */

/* 		if (_shader_parameter_set(&shader->metadata, param, result.parameters.buffer, result.parameters.textures) == false) { */
/* 			ASSERT_FORMAT(false, "shader[%.*s] invalid uniform passed (%.*s)", sarg(shader->name), sarg(param->name)); */
/* 		} */
/* 	} */

/* 	return result; */
/* } */

/* ShaderMaterial shader_material_clone(Arena *arena, ShaderMaterial *target) { */
/* 	ShaderMaterial result = { .shader = target->shader }; */

/* 	result.parameters.buffer_size = target->parameters.buffer_size; */
/* 	result.parameters.buffer = (uint8_t *)arena_push_copy( */
/* 		arena, */
/* 		target->parameters.buffer, */
/* 		result.parameters.buffer_size); */

/* 	result.parameters.texture_count = target->parameters.texture_count; */
/* 	result.parameters.textures = (Texture2D *)arena_push_copy( */
/* 		arena, */
/* 		target->parameters.textures, */
/* 		result.parameters.texture_count); */

/* 	return result; */
/* } */

/* uint32_t _shader_find_offset(String name, uint32_t set, ShaderReflection *metadata) { */
/* 	uint32_t offset = 0; */
/* 	for (uint32_t index = 0; index < metadata->sets[set].binding_count; ++index) { */
/* 		ShaderBinding *binding = &metadata->sets[set].bindings[index]; */

/* 		if (string_equals(binding->name, name)) // user asked for buffer */
/* 			return offset; */

/* 		if (binding->buffer_layout) { */
/* 			ShaderBuffer *layout = binding->buffer_layout; */
/* 			uint32_t member_offset = 0; */
/* 			for (uint32_t index = 0; index < layout->member_count; ++index) { */
/* 				ShaderBufferMember *member = &layout->members[index]; */
/* 				if (string_equals(name, member->name)) // user asked for member */
/* 					return offset + member_offset; */
/* 				else { // might not have passed . notation */
/* 					ArenaTemp scaratch = arena_scratch_begin(NULL); */
/* 					bool match = false; */
/* 					StringList list = stringlist_split(scaratch.arena, member->name, S(".")); */

/* 					if (list.first && list.first->next) */
/* 						match = string_equals(list.first->next->string, name); */

/* 					arena_scratch_end(scaratch); */
/* 					if (match) */
/* 						return offset + member_offset; */
/* 				} */
/* 				member_offset += member->size; */
/* 			} */
/* 			offset += layout->size; */
/* 		} */
/* 	} */

/* 	return (uint32_t)-1; */
/* } */

/* uint32_t _shader_find_texture_index(String name, uint32_t set, ShaderReflection *metadata) { */
/* 	uint32_t texture_index = 0; */
/* 	for (uint32_t index = 0; index < metadata->sets[set].binding_count; ++index) { */
/* 		ShaderBinding *binding = &metadata->sets[set].bindings[index]; */
/* 		bool is_texture = */
/* 			binding->type == SHADER_BINDING_IMAGE_2D || binding->type == SHADER_BINDING_IMAGE_CUBE || binding->type == SHADER_BINDING_SAMPLER; */

/* 		if (is_texture == false) */
/* 			continue; */

/* 		if (string_equals(binding->name, name)) */
/* 			return texture_index; */

/* 		texture_index++; */
/* 	} */

/* 	return (uint32_t)-1; */
/* } */

/* uint32_t _shader_parameter_to_stride(ShaderParameter *param) { */
/* 	switch (param->type) { */
/* 		case SHADER_PARAMETER_FLOAT: */
/* 			return 4; */
/* 		case SHADER_PARAMETER_FLOAT2: */
/* 			return 8; */
/* 		case SHADER_PARAMETER_FLOAT3: */
/* 			return 12; */
/* 		case SHADER_PARAMETER_FLOAT4: */
/* 			return 16; */
/* 		case SHADER_PARAMETER_FLOAT3x3: */
/* 			return 36; */
/* 		case SHADER_PARAMETER_FLOAT4x4: */
/* 			return 64; */
/* 		case SHADER_PARAMETER_BUFFER: */
/* 			return param->as.buffer.size; */
/* 		case SHADER_PARAMETER_TEXTURE: */
/* 			break; */
/* 	} */

/* 	ASSERT_MESSAGE(false, "Asked for stride of a texture"); */
/* 	return 0; */
/* } */

/* bool _shader_parameter_set(ShaderReflection *metadata, ShaderParameter *param, uint8_t *buffer, Texture2D *textures) { */
/* 	switch (param->type) { */
/* 		case SHADER_PARAMETER_FLOAT: */
/* 		case SHADER_PARAMETER_FLOAT2: */
/* 		case SHADER_PARAMETER_FLOAT3: */
/* 		case SHADER_PARAMETER_FLOAT4: */
/* 		case SHADER_PARAMETER_FLOAT3x3: */
/* 		case SHADER_PARAMETER_FLOAT4x4: */
/* 		case SHADER_PARAMETER_BUFFER: { */
/* 			uint32_t offset = _shader_find_offset(param->name, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL, metadata); */
/* 			if (offset == (uint32_t)-1) */
/* 				return false; */

/* 			memory_copy(buffer + offset, &param->as, _shader_parameter_to_stride(param)); */
/* 		} break; */

/* 		case SHADER_PARAMETER_TEXTURE: { */
/* 			uint32_t texture_index = _shader_find_texture_index(param->name, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL, metadata); */
/* 			if (texture_index == (uint32_t)-1) */
/* 				return false; */

/* 			textures[texture_index] = param->as.texture; */
/* 		} break; */
/* 	}; */

/* 	return true; */
/* } */
