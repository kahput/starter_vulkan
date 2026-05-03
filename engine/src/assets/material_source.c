#include "material_source.h"
#include "common.h"
#include "core/debug.h"
#include "renderer/r_internal.h"

MaterialSource material_source_make(Arena *arena, ShaderBinding *bindings, uint32_t binding_count) {
	MaterialSource mat = { 0 };
	mat.lookup = arena_trie_make(arena);

	for (uint32_t index = 0; index < binding_count; ++index) {
		if (bindings[index].type == SHADER_BINDING_UNIFORM_BUFFER)
			mat.buffer_size += bindings[index].buffer_layout->size;
		else if (bindings[index].type == SHADER_BINDING_TEXTURE_2D)
			mat.texture_count++;
	}

	mat.buffer_data = arena_push(arena, mat.buffer_size, 16, true);
	mat.textures = arena_array_make(arena, mat.texture_count, UUID);

	size_t current_buffer_offset = 0;
	uint32_t current_texture_slot = 0;

	for (uint32_t index = 0; index < binding_count; ++index) {
		ShaderBinding *binding = &bindings[index];

		if (binding->type == SHADER_BINDING_UNIFORM_BUFFER) {
			ShaderBuffer *layout = binding->buffer_layout;

			for (uint32_t member_index = 0; member_index < layout->member_count; ++member_index) {
				ShaderMember *member = &layout->members[member_index];
				ArenaTrieNode *node = arena_trienode_push(&mat.lookup, buffer_wrap_string(member->name));

				node->payload = mat.buffer_data + current_buffer_offset + member->offset;
			}
			current_buffer_offset += layout->size;
		} else if (binding->type == SHADER_BINDING_TEXTURE_2D) {
			ArenaTrieNode *node = arena_trienode_push(&mat.lookup, buffer_wrap_string(binding->name));

			node->payload = &mat.textures[current_texture_slot++];
		}
	}
	return mat;
}

bool material_source_setf(MaterialSource *material, String name, float value) {
	ArenaTrieNode *node = arena_trienode_find(&material->lookup, buffer_wrap_string(name));
	if (!node || !node->payload)
		return false;

	*(float *)(node->payload) = value;
	return true;
}
bool material_source_set2f(MaterialSource *material, String name, float32x2 value) {
	ArenaTrieNode *node = arena_trienode_find(&material->lookup, buffer_wrap_string(name));
	if (!node || !node->payload)
		return false;

	*(float2 *)(node->payload) = value;
	return true;
}

bool material_source_set3f(MaterialSource *material, String name, float32x3 value) {
	ArenaTrieNode *node = arena_trienode_find(&material->lookup, buffer_wrap_string(name));
	if (!node || !node->payload)
		return false;

	*(float3 *)(node->payload) = value;
	return true;
}
bool material_source_set4f(MaterialSource *material, String name, float32x4 value) {
	ArenaTrieNode *node = arena_trienode_find(&material->lookup, buffer_wrap_string(name));
	if (!node || !node->payload)
		return false;

	*(float4 *)(node->payload) = value;
	return true;
}

bool material_source_set_texture(MaterialSource *material, String name, UUID texture) {
	ArenaTrieNode *node = arena_trienode_find(&material->lookup, buffer_wrap_string(name));
	if (!node || !node->payload)
		return false;

	*(UUID *)(node->payload) = texture;
	return true;
}
