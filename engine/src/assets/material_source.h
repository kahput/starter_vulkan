#ifndef MATERIAL_SOURCE_H_
#define MATERIAL_SOURCE_H_

#include "common.h"
#include "core/astring.h"
#include "core/identifiers.h"

typedef enum {
	PROPERTY_TYPE_FLOAT,
	PROPERTY_TYPE_FLOAT2,
	PROPERTY_TYPE_FLOAT3,
	PROPERTY_TYPE_FLOAT4,

	PROPERTY_TYPE_COLOR,

	PROPERTY_TYPE_INT,
	PROPERTY_TYPE_INT2,
	PROPERTY_TYPE_INT3,
	PROPERTY_TYPE_INT4,

	PROPERTY_TYPE_IMAGE,
} PropertyType;

typedef struct material_property {
	struct material_property *children[4];
	String name;

	PropertyType type;
	union {
		float f;
		float2 f2;
		float3 f3;
		float4 f4;

		uint32 u;

		UUID image;
	} as;
} MaterialProperty;

typedef struct material_source {
	UUID shader;

	MaterialProperty *root;

	MaterialProperty *properties;
	uint32_t property_count;
} MaterialSource;

bool material_source_setf(MaterialSource material, String name, float value);
bool material_source_set2fv(MaterialSource material, String name, float2 value);
bool material_source_set3fv(MaterialSource material, String name, float3 value);
bool material_source_set4fv(MaterialSource material, String name, float4 value);
bool material_source_set_texture(MaterialSource material, String name, UUID texture);

#endif /* MATERIAL_SOURCE_H_ */
