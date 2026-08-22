#include "common.h"

typedef uint32_t TypeIdentifier;

typedef enum {
	TYPE_MEMBER_FLAG_CONST = 1 << 0,
	TYPE_MEMBER_FLAG_POINTER = 1 << 1,
	TYPE_MEMBER_FLAG_ARRAY = 1 << 2,
} MetatypeMemberFlags;

typedef struct {
	TypeIdentifier type;
	const char *field_name;

	MetatypeMemberFlags flags;
	uint32_t indirection;

	uint64_t offset, size;
	uint64_t count, stride;
} TypeMember;

typedef struct type Type;
struct type {
	TypeIdentifier type;
	const char *name;

	uint64_t size, alignment;

	TypeMember *members;
	uint32_t member_count;
};
