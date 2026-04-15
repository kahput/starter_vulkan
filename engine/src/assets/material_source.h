#ifndef MATERIAL_SOURCE_H_
#define MATERIAL_SOURCE_H_

#include "common.h"
#include "core/strings.h"
#include "core/identifiers.h"

typedef enum {
	PROPERTY_TYPE_FLOAT1,
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
		float float1;
		float32x2 float2;
		float32x3 float3;
		float32x4 float4;

		uint32 uint1;
		uint32x2 uint2;
		uint32x3 uint3;
        uint32x3 uint4;
	} as;
} MaterialProperty;

typedef struct material_source {
    MaterialProperty root;
} MaterialSource;

bool material_source_setf(MaterialSource material, String name, float value);
bool material_source_set2fv(MaterialSource material, String name, float32x2 value);
bool material_source_set3fv(MaterialSource material, String name, float32x3 value);
bool material_source_set4fv(MaterialSource material, String name, float32x4 value);
bool material_source_set_texture(MaterialSource material, String name, UUID texture);

#endif /* MATERIAL_SOURCE_H_ */
