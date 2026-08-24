#pragma once

#include "common.h"
#include <core/strings.h>

typedef enum {
	TYPE_float,
	TYPE_float32x3,

	TYPE_MAX,
} META_TypeID;

typedef struct META_Type META_Type;
typedef struct META_Field META_Field;
typedef struct META_Enumerator META_Enumerator;

typedef enum {
	META_KIND_PRIMITIVE,
	META_KIND_STRUCT,
	META_KIND_UNION,
	META_KIND_ENUM,

	META_KIND_POINTER,
	META_KIND_ARRAY,
	META_KIND_QUALIFIED,
	META_KIND_ALIAS,
} META_Kind;

typedef enum {
	META_QUAL_CONST = BIT(0),
	META_QUAL_VOLATILE = BIT(1),
	META_QUAL_RESTRICT = BIT(2),
} META_QualifierSet;

struct META_Field {
	META_TypeID type;
	String8 name;
	uint32_t offset;
};

struct META_Enumerator {
	String8 name;
	int64_t value;
};

struct META_Type {
	META_Kind kind;
	String8 name;
	uint64_t size;

	union {
		struct {
			const META_Field *fields;
			uint32_t field_count;
		} record;

		struct {
			const META_Enumerator *values;
			uint32_t value_count;
		} enumeration;

		struct {
			META_TypeID type;
		} pointer;

		struct {
			META_TypeID type;
			uint64_t count;
		} array;

		struct {
			META_TypeID type;
			META_QualifierSet qualifiers;
		} qualified;
	} as;
};

// typedef uint32_t Bitset[32];
//
// [META_Bitset] = { .kind = META_KIND_ALIAS, .as.alias = META__uint32_t_array_32 } ->
//   [META__uint32_t_array_32] = { .kind = META_KIND_ARRAY, .as.array = { .type = META_uint32_t, .count = 32 } }
//     [META_uint32_t] = { .kind = META_KIND_PRIMMITIVE }
//
//  vs
//
//  [META_Bitset] = { .kind = META_KIND_POINTER, .as.pointer = META_uint32_t_array_32 }
//    [META_uint32_t_array_32] = { .kind = META_KIND_ARRAY, .as.array = { .type = META_uint32_t, .count = 32 } }
//      [META_uint32_t] = { .kind = META_KIND_PRIMMITIVE }
