#pragma once

#include "common.h"

typedef struct META_Type META_Type;
typedef struct META_Ref META_Ref;
typedef struct META_Member META_Member;
typedef struct META_Enumerator META_Enumerator;

typedef enum {
	META_QUAL_CONST = BIT(0),
	META_QUAL_RESTRICT = BIT(1),
	META_QUAL_VOLATILE = BIT(2),
} META_Qualifiers;

typedef enum META_RefKind {
	META_REF_TERMINAL,
	META_REF_POINTER,
	META_REF_ARRAY,
} META_RefKind;

typedef enum {
	META_BUILTIN_VOID,

	META_BUILTIN_BOOL,

	META_BUILTIN_CHAR,
	META_BUILTIN_UNSIGNED_CHAR,

	META_BUILTIN_SHORT,
	META_BUILTIN_UNSIGNED_SHORT,
	META_BUILTIN_INT,
	META_BUILTIN_UNSIGNED_INT,
	META_BUILTIN_LONG,
	META_BUILTIN_UNSIGNED_LONG,
	META_BUILTIN_LONG_LONG,
	META_BUILTIN_UNSIGNED_LONG_LONG,

	META_BUILTIN_FLOAT,
	META_BUILTIN_DOUBLE,
	META_BUILTIN_LONG_DOUBLE,

	META_BUILTIN_MAX,
} META_Builtin;

struct META_Ref {
	META_RefKind kind;
	const META_Ref *next;
	META_Qualifiers qualifiers;

	union {
		const META_Type *terminal;
		uint32_t array_count;
	} as;
};

struct META_Member {
	const char *name;
	uint32_t offset;
	const META_Ref *type_ref;
};

struct META_Enumerator {
	const char *name;
	int64_t value;
};

typedef enum {
	METATYPE_KIND_BUILTIN,
	METATYPE_KIND_STRUCT,
	METATYPE_KIND_UNION,
	METATYPE_KIND_ENUM,
	METATYPE_KIND_ALIAS,

	METATYPE_KIND_MAX,
} META_TypeKind;

struct META_Type {
	const char *name;
	META_TypeKind kind;
	uint64_t size, alignment;

	union {
		META_Builtin builtin;

		struct {
			META_Member *members;
			uint32_t member_count;
		} record;

		struct {
			META_Enumerator *enumerators;
			uint32_t enumerator_count;
		} enumeration;

		struct {
			META_Ref *target;
		} alias;
	} as;
};

// This will be generated in meta_generated.c/h
typedef enum {
	TYPE_float,
	TYPE_float32x3,

	TYPE_MAX,
} TypeID;

#define type_info(T) &type_introspection[TYPE_##T]
extern const META_Type type_introspection[TYPE_MAX];
