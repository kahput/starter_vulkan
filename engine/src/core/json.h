#pragma once

#include "anim.h"
#include "arena.h"
#include "common.h"
#include "shape3.h"
#include "strings.h"

typedef enum {
	JSON_TYPE_NULL,
	JSON_TYPE_BOOLEAN,
	JSON_TYPE_STRING,
	JSON_TYPE_OBJECT,
	JSON_TYPE_ARRAY,
	JSON_TYPE_NUMBER,

	JSON_TYPE_MAX,
} JSON_Type;

typedef struct JSON_Node JSON_Node;

#define JSON_CONTAINER_INITIAL_PAGE_SIZE 4
typedef struct {
	JSON_Node *pages[32];
	uint32_t count;
} JSON_Container;

typedef struct JSON_Value {
	JSON_Type type;
	union {
		float64 number;
		String8 string;
		bool boolean;
		JSON_Container *children;
	} as;
} JSON_Value;

struct JSON_Node {
	String8 key;
	JSON_Value value;
};

#define json_object() ((JSON_Value){ .type = JSON_TYPE_OBJECT })
#define json_array() ((JSON_Value){ .type = JSON_TYPE_ARRAY })
#define json_number(v) ((JSON_Value){ .type = JSON_TYPE_NUMBER, .as.number = (v) })
#define json_string(a, s) ((JSON_Value){ .type = JSON_TYPE_STRING, .as.string = str8_copy((a), (s)) })
#define json_bool(v) ((JSON_Value){ .type = JSON_TYPE_BOOLEAN, .as.boolean = (v) })
#define json_null() ((JSON_Value){ .type = JSON_TYPE_NULL })

extern JSON_Node JSON_NIL;
static inline bool json_valid(JSON_Node *node) { return node && node != &JSON_NIL; }

JSON_Node *json_parse_string(Arena *arena, String8 source);
JSON_Node *json_parse_file(Arena *arena, String8 source);

JSON_Node *json_append_item(Arena *arena, JSON_Node *parent);
JSON_Node *json_append_field(Arena *arena, JSON_Node *parent, String8 key);

JSON_Node *json_find(JSON_Node *node, String8 key);
JSON_Node *json_find_path(JSON_Node *node, String8 path);
JSON_Node *json_child_at(JSON_Node *node, uint32_t index);

static inline bool json_is_container(JSON_Node *node) { return json_valid(node) && (node->value.type == JSON_TYPE_OBJECT || node->value.type == JSON_TYPE_ARRAY); }
static inline uint32_t json_count(JSON_Node *node) {
	return json_is_container(node) && node->value.as.children ? node->value.as.children->count : 0;
}

static inline float64 json_num_or(JSON_Node *obj, String8 key, float64 fallback) {
	JSON_Node *n = json_find(obj, key);
	return json_valid(n) && n->value.type == JSON_TYPE_NUMBER ? n->value.as.number : fallback;
}
static inline String8 json_str_or(JSON_Node *obj, String8 key, String8 fallback) {
	JSON_Node *n = json_find(obj, key);
	return json_valid(n) && n->value.type == JSON_TYPE_STRING ? n->value.as.string : fallback;
}
static inline bool json_bool_or(JSON_Node *obj, String8 key, bool fallback) {
	JSON_Node *n = json_find(obj, key);
	return json_valid(n) && n->value.type == JSON_TYPE_BOOLEAN ? n->value.as.boolean : fallback;
}

String8 json_stringify(Arena *arena, JSON_Node *root, String8 indent);

// TEMPORARY
static inline bool json_append_float4(Arena *arena, JSON_Node *target, float4 v) {
	bool ok = arena && target && json_is_container(target);
	if (ok) {
		if (target->value.type == JSON_TYPE_ARRAY) {
			json_append_item(arena, target)->value = json_number(v.x);
			json_append_item(arena, target)->value = json_number(v.y);
			json_append_item(arena, target)->value = json_number(v.z);
			json_append_item(arena, target)->value = json_number(v.w);
		} else {
			json_append_field(arena, target, s("x"))->value = json_number(v.x);
			json_append_field(arena, target, s("y"))->value = json_number(v.y);
			json_append_field(arena, target, s("z"))->value = json_number(v.z);
			json_append_field(arena, target, s("w"))->value = json_number(v.w);
		}
	}

	return ok;
}

static inline bool json_append_float3(Arena *arena, JSON_Node *target, float3 v) {
	bool ok = arena && target && json_is_container(target);
	if (ok) {
		if (target->value.type == JSON_TYPE_ARRAY) {
			json_append_item(arena, target)->value = json_number(v.x);
			json_append_item(arena, target)->value = json_number(v.y);
			json_append_item(arena, target)->value = json_number(v.z);
		} else {
			json_append_field(arena, target, s("x"))->value = json_number(v.x);
			json_append_field(arena, target, s("y"))->value = json_number(v.y);
			json_append_field(arena, target, s("z"))->value = json_number(v.z);
		}
	}

	return ok;
}

static inline bool json_append_transform3(Arena *arena, JSON_Node *target, Transform3 *t) {
	bool ok = arena && target && json_is_container(target) && t;

	if (ok) { // transform
		JSON_Node *transform = json_append_field(arena, target, s("transform"));
		transform->value = json_object();

		{
			JSON_Node *translation = json_append_field(arena, transform, s("translation"));
			translation->value = json_array();
			json_append_float3(arena, translation, t->translation);
		}

		{
			JSON_Node *rotation = json_append_field(arena, transform, s("rotation"));
			rotation->value = json_array();
			json_append_float4(arena, rotation, t->rotation);
		}

		{
			JSON_Node *scale = json_append_field(arena, transform, s("scale"));
			scale->value = json_array();
			json_append_float3(arena, scale, t->scale);
		}
	}

	return ok;
}

static inline bool json_append_shape3(Arena *arena, JSON_Node *target, Shape3 *s) {
	bool ok = arena && target && json_is_container(target) && s;
	if (ok) {
		JSON_Node *shape_node = json_append_field(arena, target, s("shape"));
		shape_node->value = json_object();

		json_append_field(arena, shape_node, s("kind"))->value = json_string(arena, shape_kind_to_string[s->kind]);
		JSON_Node *value_node = json_append_field(arena, shape_node, s("value"));
		value_node->value = json_object();

		switch (s->kind) {
			case SHAPE_KIND_AABB3: {
				JSON_Node *min = json_append_field(arena, value_node, s("min"));
				min->value = json_array();
				json_append_float3(arena, min, s->as.aabb3.min);

				JSON_Node *max = json_append_field(arena, value_node, s("max"));
				max->value = json_array();
				json_append_float3(arena, max, s->as.aabb3.max);
			} break;
			case SHAPE_KIND_SPHERE: {
				JSON_Node *center = json_append_field(arena, value_node, s("center"));
				center->value = json_array();
				json_append_float3(arena, center, s->as.sphere.center);
				json_append_field(arena, value_node, s("radius"))->value = json_number(s->as.sphere.radius);
			} break;
			case SHAPE_KIND_CAPSULE3: {
				JSON_Node *a = json_append_field(arena, value_node, s("a"));
				a->value = json_array();
				json_append_float3(arena, a, s->as.capsule.a);

				JSON_Node *b = json_append_field(arena, value_node, s("b"));
				b->value = json_array();
				json_append_float3(arena, b, s->as.capsule.b);

				json_append_field(arena, value_node, s("radius"))->value = json_number(s->as.capsule.radius);
			} break;
			case SHAPE_KIND_PLANE: {
				JSON_Node *normal = json_append_field(arena, value_node, s("normal"));
				normal->value = json_array();
				json_append_float3(arena, normal, s->as.plane.normal);

				json_append_field(arena, value_node, s("distance"))->value = json_number(s->as.plane.distance);
			} break;
			default:
				break;
		}
	}

	return ok;
}
