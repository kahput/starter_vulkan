#ifndef MATERIAL_SOURCE_H_
#define MATERIAL_SOURCE_H_

#include "common.h"
#include "core/strings.h"
#include "core/identifiers.h"
#include "renderer/r_internal.h"

typedef struct material_source {
	ArenaTrie lookup;

	uint8_t *buffer_data;
	size_t buffer_size;

	uint32_t texture_count;
	UUID *textures;
} MaterialSource;

MaterialSource material_source_make(Arena *arena, ShaderBinding *bindings, uint32_t binding_count);

bool material_source_setf(MaterialSource *material, String name, float value);
bool material_source_set2f(MaterialSource *material, String name, float32x2 value);
bool material_source_set3f(MaterialSource *material, String name, float32x3 value);
bool material_source_set4f(MaterialSource *material, String name, float32x4 value);
bool material_source_set_texture(MaterialSource *material, String name, UUID texture);

#endif /* MATERIAL_SOURCE_H_ */
