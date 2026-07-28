#include "app/scene.h"

#include "common.h"
#include "core/geom.h"
#include "core/debug.h"
#include "core/arena.h"
#include "core/logger.h"
#include "core/cmath.h"
#include "core/strings.h"
#include "core/input_types.h"

#include "utils/anim.h"
#include "utils/json.h"
#include "utils/lexer.h"

#include "input.h"
#include "os.h"

#include "gfx.h"
#include "gfx/font.h"
#include "gfx/imgui.h"
#include "gfx/gfx_types.h"
#include "gfx/vulkan/tables.h"

#include <math.h>

#include <stdarg.h>
#include <cgltf/cgltf.h>
#include <stb/stb_image.h>
#include <vulkan/vulkan_core.h>
#include <cglm/cglm.h>

typedef enum {
	ICON_PLAY,
	ICON_PAUSE,
	ICON_STOP,

	ICON_MAX,
} IconID;

String8 icon_to_filepath[ICON_MAX] = {
	[ICON_PLAY] = str_comp("assets/icons/PNG/White/1x/forward.png"),
	[ICON_PAUSE] = str_comp("assets/icons/PNG/White/1x/pause.png"),
	[ICON_STOP] = str_comp("assets/icons/PNG/White/1x/stop.png")
};

String8 icon_to_string[ICON_MAX] = {
	ENUM_STRING_TABLE_ENTRY(ICON, PLAY),
	ENUM_STRING_TABLE_ENTRY(ICON, PAUSE),
	ENUM_STRING_TABLE_ENTRY(ICON, STOP),
};

typedef struct {
	float2 position, uv;
	float4 radii;
	float2 size;
	uint32_t fill_color, border_color;
	float border_width;
	uint32_t imageid;
} QuadVertex2D;

typedef struct {
	float4 a, b; // xyz + thickness
	uint32_t color;
	float3 _pad0;
} LineVertex3D;

typedef struct {
	float3 position;
	float _pad0;
	float3 normal;
	float _pad1;
	float2 uv;
	float4 tangent;
} Vertex3D;

typedef struct {
	uint4 bone_ids;
	float4 weights;
} SkinningVertex3D;

typedef enum {
	TEXTURE_SLOT_ALBEDO,
	TEXTURE_SLOT_METAL_ROUGHNESS,
	TEXTURE_SLOT_NORMAL,
	TEXTURE_SLOT_OCCLUSION,
	TEXTURE_SLOT_EMISSIVE,

	TEXTURE_SLOT_COUNT,
} TextureSlot;

String8 texture_slot_to_string[TEXTURE_SLOT_COUNT] = {
	[TEXTURE_SLOT_ALBEDO] = str_comp("albedo"),
	[TEXTURE_SLOT_METAL_ROUGHNESS] = str_comp("metal_roughness"),
	[TEXTURE_SLOT_NORMAL] = str_comp("normal"),
	[TEXTURE_SLOT_OCCLUSION] = str_comp("occlusion"),
	[TEXTURE_SLOT_EMISSIVE] = str_comp("emissive"),
};

Rectangle image_rect(Image2D image) { return (Rectangle){ 0, 0, image.width, image.height }; }
float2 image_size(Image2D image) { return (float2){ image.width, image.height }; }

typedef enum {
	DRAW_PASS_OPAQUE,
	DRAW_PASS_TRANSPARENT,

	DRAW_PASS_MAX,
} DrawPass;

static String8 draw_pass_to_string[DRAW_PASS_MAX] = {
	ENUM_STRING_TABLE_ENTRY(DRAW_PASS, OPAQUE),
	ENUM_STRING_TABLE_ENTRY(DRAW_PASS, TRANSPARENT),
};

typedef struct {
	Image2D textures[TEXTURE_SLOT_COUNT];

	float4 tint, emissive;
	float2 metallic_roughness;
} Material;

typedef struct {
	uint32_t vertex_offset, index_offset;
	uint32_t vertex_count, index_count;
	uint32_t material_id;

	AABB3 bounds;
} MeshPart;

typedef struct {
	// CPU
	Vertex3D *vertices;
	SkinningVertex3D *skinning;
	uint32_t *indices;

	// GPU
	GFX_Buffer *buffer;
	uint64_t buffer_vertex_byte_offset;
	uint64_t buffer_index_byte_offset;
	uint64_t buffer_skinning_data_byte_offset;

	Material *materials;
	uint32_t material_count;

	// METADATA
	MeshPart *parts;
	uint32_t part_count;
	Skeleton skeleton;

	uint64_t total_vertex_count;
	uint64_t total_index_count;

	AABB3 bounds;
} Mesh;

typedef enum {
	MESH_HERO_MALE,
	MESH_GDBOT,
	MESH_MAGE,
	MESH_BARREL,
	MESH_ROOM,
	MESH_TEST_LEVEL,

	MESH_ROOM_LARGE,
	MESH_TERRAIN_FLAT,
	MESH_TERRAIN_HEIGHTMAP,
	MESH_GRASS_BILLBOARD,

	MESH_CYLINDER,
	MESH_SPHERE,
	MESH_GIZMOS_ARROW,

	MESH_MAX,
} MeshID;

static const String8 meshid_to_string[MESH_MAX] = {
	ENUM_STRING_TABLE_ENTRY(MESH, HERO_MALE),
	ENUM_STRING_TABLE_ENTRY(MESH, GDBOT),
	ENUM_STRING_TABLE_ENTRY(MESH, MAGE),
	ENUM_STRING_TABLE_ENTRY(MESH, BARREL),
	ENUM_STRING_TABLE_ENTRY(MESH, ROOM),
	ENUM_STRING_TABLE_ENTRY(MESH, TEST_LEVEL),
	ENUM_STRING_TABLE_ENTRY(MESH, ROOM_LARGE),
	ENUM_STRING_TABLE_ENTRY(MESH, TERRAIN_FLAT),
	ENUM_STRING_TABLE_ENTRY(MESH, TERRAIN_HEIGHTMAP),
	ENUM_STRING_TABLE_ENTRY(MESH, GRASS_BILLBOARD),
	ENUM_STRING_TABLE_ENTRY(MESH, CYLINDER),
	ENUM_STRING_TABLE_ENTRY(MESH, SPHERE),
	ENUM_STRING_TABLE_ENTRY(MESH, GIZMOS_ARROW),
};

typedef struct {
	MeshID id;
	Transform3 transform;
	bool cast_shadow;

	// anim
	uint64_t skinned_vertices_offset;
	float4x4 *skin_matrices;
} MeshInstance;

String8 meshid_to_metadata[MESH_MAX] = {
	[MESH_HERO_MALE] = str_comp("assets/models/hero_male.glb"),
	[MESH_GDBOT] = str_comp("assets/models/gdbot.glb"),
	[MESH_MAGE] = str_comp("assets/models/mage.glb"),
	[MESH_BARREL] = str_comp("assets/models/barrel.glb"),
	[MESH_ROOM] = str_comp("assets/models/room.glb"),
	[MESH_ROOM_LARGE] = str_comp("assets/models/room-large.glb"),
	[MESH_TEST_LEVEL] = str_comp("assets/models/test_level.glb"),
	[MESH_GRASS_BILLBOARD] = str_comp("assets/models/grass.glb"),
};

typedef enum {
	ENTITY_FEATURE_DRAW_MESH,
	ENTITY_FEATURE_CAST_SHADOW,

	// Gameplay
	ENTITY_FEATURE_PLAYER_CONTROLLED,
	ENTITY_FEATURE_INTERACTABLE,
	ENTITY_FEATURE_COLLIDABLE,
	ENTITY_FEATURE_FOLLOW_TARGET,

	ENTITY_FEATURE_ANIMATE,

	ENTITY_FEATURE_MAX,
} EntityFeature;

static const String8 entity_feature_to_string[ENTITY_FEATURE_MAX] = {
	ENUM_STRING_TABLE_ENTRY(ENTITY_FEATURE, DRAW_MESH),
	ENUM_STRING_TABLE_ENTRY(ENTITY_FEATURE, CAST_SHADOW),
	ENUM_STRING_TABLE_ENTRY(ENTITY_FEATURE, PLAYER_CONTROLLED),
	ENUM_STRING_TABLE_ENTRY(ENTITY_FEATURE, INTERACTABLE),
	ENUM_STRING_TABLE_ENTRY(ENTITY_FEATURE, COLLIDABLE),
	ENUM_STRING_TABLE_ENTRY(ENTITY_FEATURE, FOLLOW_TARGET),
	ENUM_STRING_TABLE_ENTRY(ENTITY_FEATURE, ANIMATE),
};

#define ENTITY_BITSET_WIDTH 64
#define ENTITY_BITSET_SIZE (ENTITY_FEATURE_MAX + 63) / 64
typedef uint64_t FeatureBitset[ENTITY_BITSET_SIZE];
typedef struct Entity {
	FeatureBitset features;

	MeshID meshid;
	DrawPass pass;
	Transform3 transform;

	// skinning
	uint64_t skinned_vertices_offset;
	float4x4 *skin_matrices;

	uint32_t current_anim;
	float anim_t, blend_t;

	// gameplay
	float interact_radius;
	Shape3 shape;

	float move_speed;

	struct Entity *target;
} Entity;

#define MAX_ENTITIES 1024
typedef struct {
	Entity entities[MAX_ENTITIES];
	uint32_t entity_count;
} World;

// :functions
Image2D load_image(Arena *arena, String8 path);
Image2D load_cubemap(Arena *arena, String8 paths[], uint32_t count);
Mesh load_gltf(Arena *arena, String8 path);

Mesh mesh_sphere(Arena *arena, float3 origin, float radius, uint32_t segments, uint32_t rings);
Mesh mesh_cylinder(Arena *arena, float3 origin, float half_height, float bottom_radius, float top_radius, uint32_t segments, uint32_t rings, bool top_cap, bool bottom_cap);
Mesh mesh_cone(Arena *arena, float3 origin, float height, float radius, uint32_t segments);

Mesh mesh_plane(Arena *arena, Plane p, float width, float height, uint32_t subdivision_x, uint32_t subdivision_z);
Mesh mesh_heightmap(Arena *arena, Side orientation, float w, float h, Image2D heightmap);

Mesh mesh_merge(Arena *arena, Mesh *meshes, uint32_t mesh_count);

AnimationClip *load_gltf_animations(Arena *arena, String8 path, uint32_t *count);

// corners[] = { top-left, top-right, bottom-left, bottom-right }
void draw2d_quad(Arena *arena, Rectangle dst, Rectangle src, Image2D *image, float2 origin, float rotation, float border_width, Color border_color, float4 radii, Color fill_color);
void draw2d_rect(Arena *arena, Rectangle rect, Color color);
void draw2d_rect_ex(Arena *arena, Rectangle rect, float2 origin, float rotation, Color color);
void draw2d_rect_outline(Arena *arena, Rectangle rect, float thickness, Color color);

void draw2d_rect_rounded(Arena *arena, Rectangle rect, float4 radii, Color color);
void draw2d_rect_rounded_ex(Arena *arena, Rectangle rect, float2 origin, float rotation, float4 radii, Color color);

void draw2_sprite_ex(Arena *arena, Rectangle src, Rectangle dst, Image2D *image, Color tint);
void draw2_sprite(Arena *arena, float2 position, Image2D *image, Color tint);

void draw2d_point(Arena *arena, float2 position, float radius, Color color);
void draw2d_textf(Arena *arena, Font *font, float2 position, Color color, String8 format, ...);

void draw2_line(Arena *arena, float2 start, float2 end, float thickness, Color color);
void draw2_arrow(Arena *arena, float2 start, float2 end, float thickness, Color color);
void draw2_triangle(Arena *arena, Triangle2 triangle, float thickness, Color color);

void draw3_line(Arena *arena, float3 start, float3 end, float thickness, Color color);
void draw3_arrow(Arena *arena, float3 start, float3 end, float thickness, Color color,
	float4x4 view, float4x4 projeciton, float viewport_width);
void draw3_arc(Arena *arena, float3 center, float radius, uint8_t segments, Side plane, float angle_span, float thickness, Color color);
void draw3_sphere_outline(Arena *arena, float3 center, float radius, uint8_t segments, float thickness, Color color);
void draw3_capsule_outline(Arena *arena, float3 a, float3 b, float radius, uint8_t segments, float thickness, Color color);
void draw3_aabb_outline(Arena *arena, AABB3 aabb3, float thickness, Color color);
void draw3_triangle_outline(Arena *arena, Triangle3 t, float thickness, Color color);
void draw3_quad_outline(Arena *arena, Plane plane, float width, float height, float thickness, Color color);
void draw3_shape_outline(Arena *arena, Shape3 *shape, float3 offset, float thickness);

uint32_t find_animation(AnimationClip *clips, uint32_t count, String8 target) {
	for (uint32_t anim_index = 0; anim_index < count; ++anim_index)
		if (str8_equals(str8_wrap(clips[anim_index].name), target))
			return anim_index;

	return 0;
}

Entity *entity_spawn(World *world) {
	Entity *result = 0;
	bool ok = world;

	if (ok) {
		result = &world->entities[0];

		ok = world->entity_count < MAX_ENTITIES;
	}

	if (ok) {
		result = &world->entities[world->entity_count++];

		result->transform.rotation = quat4_identity();
		result->transform.scale = one3;
	}

	if (ok == false) {
		ASSERT(false);
	}

	return result;
}

static inline void entity__bitset_flip(FeatureBitset bitset, EntityFeature feature, bool on) {
	uint32_t index = feature / ENTITY_BITSET_WIDTH;

	if (on)
		bitset[index] |= 1ULL << (feature % ENTITY_BITSET_WIDTH);
	else
		bitset[index] &= ~(1ULL << (feature % ENTITY_BITSET_WIDTH));
}
static inline bool entity__bitset_test(FeatureBitset bitset, EntityFeature feature) {
	uint32_t index = feature / ENTITY_BITSET_WIDTH;
	return bitset[index] & (1ULL << (feature % ENTITY_BITSET_WIDTH));
}

void entity_enable(Entity *entity, EntityFeature feature) {
	bool ok = entity && feature < ENTITY_FEATURE_MAX;
	if (ok)
		entity__bitset_flip(entity->features, feature, true);
}
void entity_disable(Entity *entity, EntityFeature feature) {
	bool ok = entity && (uint32_t)feature < ENTITY_FEATURE_MAX;
	if (ok)
		entity__bitset_flip(entity->features, feature, false);
}

static inline bool entity_has(Entity *entity, EntityFeature feature) {
	bool ok = entity && (uint32_t)feature < ENTITY_FEATURE_MAX;
	if (ok)
		ok = entity__bitset_test(entity->features, feature);

	return ok;
}

int32_t cmp_mesh_sort(const void *p1, const void *p2) {
	const struct {
		float distance;
		Entity *mesh;
	} *m1 = p1, *m2 = p2;

	if (m2->distance > m1->distance)
		return 1;
	if (m2->distance < m1->distance)
		return -1;
	return 0;
}

static inline bool json_append_transform3(Arena *arena, JSON_Node *target, Transform3 *t) {
	bool ok = arena && target && json_is_container(target) && t;
	if (ok) { // transform
		JSON_Node *transform = json_append_field(arena, target, s("transform"));
		transform->value = json_object();

		json_append_float3(arena, transform, s("translation"), t->translation);
		json_append_float4(arena, transform, s("rotation"), t->rotation);
		json_append_float3(arena, transform, s("scale"), t->scale);
	}

	return ok;
}

#define WORLD_MAGIC_NUMBER 0x5102
JSON_Node *world_to_json(Arena *arena, World *world) {
	ArenaTemp scratch = arena_scratch_begin(arena);
	JSON_Node *result = &JSON_NIL;

	bool ok = arena && world;

	if (ok) {
		result = arena_push_count(arena, JSON_Node, 1);
		result->value = json_object();

		json_append_field(arena, result, s("magic"))->value = json_number(WORLD_MAGIC_NUMBER);
		json_append_field(arena, result, s("entity_count"))->value = json_number(world->entity_count - 1);
		JSON_Node *arr = json_append_field(arena, result, s("entities"));
		arr->value = json_array();

		for (uint32_t index = 1; index < world->entity_count; ++index) {
			Entity *entity = &world->entities[index];

			JSON_Node *entity_node = json_append_item(arena, arr);
			entity_node->value = json_object();

			String8 feature_flags = { 0 };
			for (EntityFeature feature = 0; feature < ENTITY_FEATURE_MAX; ++feature) {
				if (entity_has(entity, feature)) {
					if (feature_flags.length != 0 && feature_flags.text[feature_flags.length - 1] != ' ') {
						feature_flags = str8_concat(scratch.arena, feature_flags, s(" | "));
					}

					feature_flags = str8_concat(scratch.arena, feature_flags, entity_feature_to_string[feature]);
				}
			}
			json_append_field(arena, entity_node, s("features"))->value = json_string(arena, feature_flags);

			json_append_transform3(arena, entity_node, &entity->transform);
			json_append_field(arena, entity_node, s("meshid"))->value = json_string(arena, meshid_to_string[entity->meshid]);

			if (entity_has(entity, ENTITY_FEATURE_COLLIDABLE)) {
				Shape3 *s = &entity->shape;

				JSON_Node *shape_node = json_append_field(arena, entity_node, s("shape"));
				shape_node->value = json_object();

				json_append_field(arena, shape_node, s("kind"))->value = json_string(arena, shape_kind_to_string[s->kind]);
				JSON_Node *value_node = json_append_field(arena, shape_node, s("value"));
				value_node->value = json_object();

				switch (s->kind) {
					case SHAPE_KIND_AABB3: {
						json_append_float3(arena, value_node, s("min"), s->as.aabb3.min);
						json_append_float3(arena, value_node, s("max"), s->as.aabb3.max);
					} break;
					case SHAPE_KIND_SPHERE: {
						json_append_float3(arena, value_node, s("center"), s->as.sphere.center);
						json_append_field(arena, value_node, s("radius"))->value = json_number(s->as.sphere.radius);
					} break;
					case SHAPE_KIND_CAPSULE3: {
						json_append_float3(arena, value_node, s("a"), s->as.capsule.a);
						json_append_float3(arena, value_node, s("b"), s->as.capsule.b);
						json_append_field(arena, value_node, s("radius"))->value = json_number(s->as.capsule.radius);
					} break;
					case SHAPE_KIND_PLANE: {
						json_append_float3(arena, value_node, s("normal"), s->as.plane.normal);
						json_append_field(arena, value_node, s("distance"))->value = json_number(s->as.plane.distance);
					} break;
					default:
						break;
				}
			}
			json_append_field(arena, entity_node, s("pass"))->value = json_string(arena, draw_pass_to_string[entity->pass]);

			if (entity->target)
				json_append_field(arena, entity_node, s("target"))->value = json_number(indexof(world->entities, entity->target) - 1);
			if (entity_has(entity, ENTITY_FEATURE_INTERACTABLE))
				json_append_field(arena, entity_node, s("interact_radius"))->value = json_number(entity->interact_radius);
		}
	}

	arena_scratch_end(scratch);
	return result;
}

bool json_to_world(JSON_Node *root, World *world) {
	bool ok = root && json_num_or(json_find(root, s("magic")), 0) == WORLD_MAGIC_NUMBER;
	if (ok) {
		JSON_Node *entities = json_find(root, s("entities"));
		world->entity_count = json_count(entities) + 1;

		for (uint32_t node_index = 0; node_index < json_count(entities); ++node_index) {
			JSON_Node *entity_node = json_child_at(entities, node_index);
			Entity *entity = &world->entities[node_index + 1];

			JSON_Node *transform = json_find(entity_node, s("transform"));
			if (json_valid(transform)) { // transform
				JSON_Node *translation = json_find(transform, s("translation"));
				entity->transform.translation.x = json_num_or(json_child_at(translation, 0), 0.0f);
				entity->transform.translation.y = json_num_or(json_child_at(translation, 1), 0.0f);
				entity->transform.translation.z = json_num_or(json_child_at(translation, 2), 0.0f);

				JSON_Node *rotation = json_find(transform, s("rotation"));
				entity->transform.rotation.x = json_num_or(json_child_at(rotation, 0), 0.0f);
				entity->transform.rotation.y = json_num_or(json_child_at(rotation, 1), 0.0f);
				entity->transform.rotation.z = json_num_or(json_child_at(rotation, 2), 0.0f);
				entity->transform.rotation.w = json_num_or(json_child_at(rotation, 3), 0.0f);

				JSON_Node *scale = json_find(transform, s("scale"));
				entity->transform.scale.x = json_num_or(json_child_at(scale, 0), 1.0f);
				entity->transform.scale.y = json_num_or(json_child_at(scale, 1), 1.0f);
				entity->transform.scale.z = json_num_or(json_child_at(scale, 2), 1.0f);
			}

			{ // features
				String8 features = json_str_or(json_find(entity_node, s("features")), s(""));
				Lexer lexer = lexer_make(features, (String8 *)entity_feature_to_string, countof(entity_feature_to_string));
				Token t = { 0 };
				while ((t = lexer_next(&lexer)).type != TOKEN_EOF) {
					if (t.type >= TOKEN_KEYWORD_0 && t.type < TOKEN_KEYWORD_0 + ENTITY_FEATURE_MAX)
						entity_enable(entity, t.type - TOKEN_KEYWORD_0);
				}
			}

			String8 meshid = json_str_or(json_find(entity_node, s("meshid")), s(""));
			if (meshid.length)
				for (uint32_t index = 0; index < MESH_MAX; ++index) {
					if (str8_equals(meshid, meshid_to_string[index])) {
						entity->meshid = index;
						break;
					}
				}

			JSON_Node *shape = json_find(entity_node, s("shape"));
			if (json_valid(shape)) {
				String8 kind = json_str_or(json_find(shape, s("kind")), s(""));

				if (kind.length)
					for (uint32_t index = 0; index < SHAPE_KIND_MAX; ++index) {
						if (str8_equals(kind, shape_kind_to_string[index])) {
							entity->shape.kind = index;
							break;
						}
					}

				JSON_Node *shape_value = json_find(shape, s("value"));
				if (json_valid(shape_value))
					switch (entity->shape.kind) {
						case SHAPE_KIND_AABB3: {
							JSON_Node *min = json_find(shape_value, s("min"));
							entity->shape.as.aabb3.min.x = json_num_or(json_child_at(min, 0), -0.5f);
							entity->shape.as.aabb3.min.y = json_num_or(json_child_at(min, 1), -0.5f);
							entity->shape.as.aabb3.min.z = json_num_or(json_child_at(min, 2), -0.5f);

							JSON_Node *max = json_find(shape_value, s("max"));
							entity->shape.as.aabb3.max.x = json_num_or(json_child_at(max, 0), 0.5f);
							entity->shape.as.aabb3.max.y = json_num_or(json_child_at(max, 1), 0.5f);
							entity->shape.as.aabb3.max.z = json_num_or(json_child_at(max, 2), 0.5f);
						} break;

						case SHAPE_KIND_SPHERE: {
							JSON_Node *center = json_find(shape_value, s("center"));
							entity->shape.as.sphere.center.x = json_num_or(json_child_at(center, 0), 0.0f);
							entity->shape.as.sphere.center.y = json_num_or(json_child_at(center, 1), 0.0f);
							entity->shape.as.sphere.center.z = json_num_or(json_child_at(center, 2), 0.0f);

							entity->shape.as.sphere.radius = json_num_or(json_find(shape_value, s("radius")), 1.0f);

						} break;
						case SHAPE_KIND_CAPSULE3: {
							JSON_Node *a = json_find(shape_value, s("a"));
							entity->shape.as.capsule.a.x = json_num_or(json_child_at(a, 0), -0.5f);
							entity->shape.as.capsule.a.y = json_num_or(json_child_at(a, 1), -0.5f);
							entity->shape.as.capsule.a.z = json_num_or(json_child_at(a, 2), -0.5f);

							JSON_Node *b = json_find(shape_value, s("b"));
							entity->shape.as.capsule.b.x = json_num_or(json_child_at(b, 0), 0.5f);
							entity->shape.as.capsule.b.y = json_num_or(json_child_at(b, 1), 0.5f);
							entity->shape.as.capsule.b.z = json_num_or(json_child_at(b, 2), 0.5f);

							entity->shape.as.capsule.radius = json_num_or(json_find(shape_value, s("radius")), 1.0f);
						} break;
						case SHAPE_KIND_PLANE: {
							JSON_Node *normal = json_find(shape_value, s("normal"));
							entity->shape.as.plane.normal.x = json_num_or(json_child_at(normal, 0), 0.0f);
							entity->shape.as.plane.normal.y = json_num_or(json_child_at(normal, 1), 1.0f);
							entity->shape.as.plane.normal.z = json_num_or(json_child_at(normal, 2), 0.0f);

							entity->shape.as.plane.distance = json_num_or(json_find(shape_value, s("distance")), 0.0f);
						} break;
						default:
							break;
					}
			}

			{
				String8 draw_pass = json_str_or(json_find(entity_node, s("pass")), s(""));
				if (draw_pass.length)
					for (DrawPass pass = 0; pass < DRAW_PASS_MAX; ++pass) {
						if (str8_equals(draw_pass, draw_pass_to_string[pass])) {
							entity->pass = pass;
							break;
						}
					}
			}
			entity->interact_radius = json_num_or(json_find(entity_node, s("interact_radius")), 0.0f);
			JSON_Node *target_node = json_find(entity_node, s("target"));
			if (json_valid(target_node)) {
				int32_t target_index = (int32_t)json_num_or(json_find(entity_node, s("target")), 0);
				entity->target = &world->entities[target_index + 1];
			} else
				entity->target = &world->entities[0];
		}
	}

	return ok;
}

bool check_for_overlap(Entity *entity, Entity *entities, uint32_t entity_count) {
	bool result = false;

	for (uint32_t entity_index = 0; entity_index < entity_count; ++entity_index) {
		Entity *other = &entities[entity_index];
		if (other == entity || entity_has(other, ENTITY_FEATURE_COLLIDABLE) == false || other->shape.kind != SHAPE_KIND_AABB3)
			continue;

		AABB3 other_aabb = aabb3_from_center(
			add3(aabb3_center(other->shape.as.aabb3), other->transform.translation),
			mul3(aabb3_half_extent(other->shape.as.aabb3), entity->transform.scale) //
		);

		AABB3 entity_aabb = aabb3_from_center(
			add3(aabb3_center(entity->shape.as.aabb3), entity->transform.translation),
			mul3(aabb3_half_extent(entity->shape.as.aabb3), entity->transform.scale) //
		);

		if (aabb3_overlap(other_aabb, entity_aabb))
			result = true;
	}

	return result;
}

int main(void) {
	logger_set_level(LOG_LEVEL_DEBUG);

	os_display_startup();
	OS_Surface *main_render = os_surface_open(1280, 720, s("main_render"), OS_SURFACE_FLAG_RESIZEABLE);
	OS_Surface *popup_compute = os_surface_open(640, 360, s("popup_compute"), 0);

	uint64_t start_time = os_time_ns();

	InputState input_state = { 0 };
	input_set_context(&input_state);

	Arena permanent[] = { arena_make(MiB(256)) };
	GFX_Device device[] = { 0 };

	if (gfx_device_make(device) == false)
		return 0;

	GFX_Swapchain *main_swapchain = gfx_swapchain_make(device, main_render, "main");
	GFX_Swapchain *popup_swapchain = gfx_swapchain_make(device, popup_compute, "popup");
	os_surface_close(popup_compute);

	// :targets
	GFX_Image *compute_image = gfx_image_make(device, 640, 360,
		(ImageOptions){
		  .debug_name = "target:compute",
		  .format = PIXEL_FORMAT_RGBA16_FLOAT,
		  .usage = IMAGE_USAGE_STORAGE | IMAGE_USAGE_TRANSFER,
		});

	GFX_Image *msaa_target = gfx_image_make(device, 1280, 720,
		(ImageOptions){
		  .debug_name = "target:scratch_msaa",
		  .format = PIXEL_FORMAT_RGBA8_SRGB,
		  .usage = IMAGE_USAGE_RENDER,
		  .sample = SAMPLE_COUNT_8,
		});
	GFX_Image *spatial_target = gfx_image_make(device, 1280, 720,
		(ImageOptions){
		  .debug_name = "target:main_color",
		  .format = PIXEL_FORMAT_RGBA8_SRGB,
		  .usage = IMAGE_USAGE_RENDER | IMAGE_USAGE_SAMPLE,
		  .sample = SAMPLE_COUNT_1,
		});

	GFX_Image *ui_target = gfx_image_make(device, 1280, 720,
		(ImageOptions){
		  .debug_name = "target:ui",
		  .format = PIXEL_FORMAT_RGBA8_UNORM,
		  .usage = IMAGE_USAGE_RENDER | IMAGE_USAGE_SAMPLE,
		  .sample = SAMPLE_COUNT_1,
		});
	GFX_Image *depthbuffer = gfx_image_make(device, 1280, 720,
		(ImageOptions){
		  .debug_name = "target:main_depth",
		  .format = PIXEL_FORMAT_DEPTH,
		  .usage = IMAGE_USAGE_RENDER,
		  .sample = SAMPLE_COUNT_8,
		});
	GFX_Image *shadow_depthbuffer = gfx_image_make(device, 2048, 2048,
		(ImageOptions){
		  .debug_name = "target:shadow",
		  .format = PIXEL_FORMAT_DEPTH,
		  .usage = IMAGE_USAGE_RENDER | IMAGE_USAGE_SAMPLE,
		});

	Image2D skybox = load_cubemap(permanent,
		array_arg(
			String8,
			s("assets/textures/skybox_mc/dayRight.png"),
			s("assets/textures/skybox_mc/dayLeft.png"),
			s("assets/textures/skybox_mc/dayTop.png"),
			s("assets/textures/skybox_mc/dayBottom.png"),
			s("assets/textures/skybox_mc/dayFront.png"),
			s("assets/textures/skybox_mc/dayBack.png") //
			) //
	);
	skybox.handle = gfx_image_make(device, skybox.width, skybox.height,
		(ImageOptions){
		  .debug_name = "skybox",
		  .format = PIXEL_FORMAT_RGBA8_SRGB,
		  .type = IMAGE_TYPE_CUBE,
		  .pixels = skybox.pixels,
		});

	typedef enum {
		FONT_BAKE_SIZE_8,
		FONT_BAKE_SIZE_12,
		FONT_BAKE_SIZE_16,
		FONT_BAKE_SIZE_24,
		FONT_BAKE_SIZE_32,
		FONT_BAKE_SIZE_64,

		FONT_BAKE_SIZE_MAX,
	} FONT_BakeSize;
	uint32_t font_bake_size_to_value[FONT_BAKE_SIZE_MAX] = { 8, 12, 16, 24, 32, 64 };

	Font fonts[FONT_BAKE_SIZE_MAX] = { 0 };
	{ // :fonts
		ArenaTemp scratch = arena_scratch_begin(0);
		uint32_t font_cursor = 0;

		for (FONT_BakeSize bake_size_index = 0; bake_size_index < FONT_BAKE_SIZE_MAX; ++bake_size_index) {
			Font *font = &fonts[bake_size_index];
			*font = load_font(scratch.arena, s("assets/fonts/PixeloidSans.ttf"), font_bake_size_to_value[bake_size_index]);
			Glyph *glyphs = font->glyphs;
			font->glyphs = arena_push_count(permanent, Glyph, font->glyph_count);

			memory_copy(font->glyphs, glyphs, sizeof(Glyph) * font->glyph_count);

			Image2D *atlas = &fonts[bake_size_index].atlas;
			atlas->handle = gfx_image_make(device, atlas->width, atlas->height,
				(ImageOptions){
				  .debug_name = (char *)str8_pushf(scratch.arena, s("font:%d"), font_bake_size_to_value[bake_size_index]).text,
				  .format = PIXEL_FORMAT_RGBA8_UNORM,
				  .pixels = atlas->pixels,
				});
		}

		arena_scratch_end(scratch);
	}

	Image2D icons[ICON_MAX] = { 0 };
	{ // :icons
		ArenaTemp scratch = arena_scratch_begin(0);
		for (uint32_t index = 0; index < ICON_MAX; ++index) {
			icons[index] = load_image(scratch.arena, icon_to_filepath[index]);

			Image2D *icon = &icons[index];
			icon->handle = gfx_image_make(device, icon->width, icon->height,
				(ImageOptions){
				  .debug_name = (char *)icon_to_string[index].text,
				  .format = PIXEL_FORMAT_RGBA8_UNORM,
				  .pixels = icon->pixels,
				});
		}
		arena_scratch_end(scratch);
	}
	Image2D terrain_texture = load_image(permanent, s("assets/textures/base_grass.png"));
	Image2D grid_texture = load_image(permanent, s("assets/textures/prototype/texture_09.png"));
	grid_texture.handle = gfx_image_make(device, grid_texture.width, grid_texture.height,
		(ImageOptions){
		  .debug_name = "grid",
		  .format = PIXEL_FORMAT_RGBA8_SRGB,
		  .pixels = grid_texture.pixels,
		});
	Image2D window_texture = load_image(permanent, s("assets/textures/blending_transparent_window.png"));
	window_texture.handle = gfx_image_make(device, window_texture.width, window_texture.height,
		(ImageOptions){
		  .debug_name = "window",
		  .format = PIXEL_FORMAT_RGBA8_SRGB,
		  .pixels = window_texture.pixels,
		});

	Image2D noise_image = load_image(permanent, s("assets/textures/heightmap.png"));
	noise_image.handle = gfx_image_make(device, noise_image.width, noise_image.height,
		(ImageOptions){
		  .debug_name = "noise",
		  .format = PIXEL_FORMAT_RGBA8_UNORM,
		  .pixels = noise_image.pixels,
		});

	GFX_Sampler *linear_sampler[WRAP_MODE_COUNT] = {
		[WRAP_MODE_REPEAT] = gfx_sampler_make(device, sampler_opt("default:linear_repeat", FILTER_LINEAR, WRAP_MODE_REPEAT)),
		[WRAP_MODE_CLAMP] = gfx_sampler_make(device, sampler_opt("default:linear_clamp", FILTER_LINEAR, WRAP_MODE_CLAMP))
	};
	GFX_Sampler *nearest_sampler[WRAP_MODE_COUNT] = {
		[WRAP_MODE_CLAMP] = gfx_sampler_make(device, sampler_opt("default:nearest_clamp", FILTER_NEAREST, WRAP_MODE_CLAMP)),
		[WRAP_MODE_CLAMP_BORDER] = gfx_sampler_make(device, sampler_opt("default:nearest_clamp_border", FILTER_NEAREST, WRAP_MODE_CLAMP_BORDER))
	};
	SamplerOptions shadow_opt = sampler_opt("default:shadow", FILTER_LINEAR, WRAP_MODE_CLAMP_BORDER);
	/* shadow_opt.compare_enable = true; */
	GFX_Sampler *shadow_sampler = gfx_sampler_make(device, shadow_opt);
	GFX_Image *white_texture = gfx_image_make(device, 1, 1,
		(ImageOptions){
		  .debug_name = "default:white",
		  .format = PIXEL_FORMAT_RGBA8_SRGB,
		  .pixels = &(uint32_t){ 0xFFFFFFFF },
		});

	// create compute pipeline
	OS_Timestamp compute_ts = os_file_last_modified(s("assets/shaders/compute/bin/test.compute.spv"));
	GFX_Pipeline c_pipeline = { 0 };
	{
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 compute_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/compute/bin/test.compute.spv"));
		c_pipeline = compute_pipeline_make(device, compute_bytecode);
		arena_scratch_end(scratch);
	}

	GFX_Pipeline pipeline_skinning = { 0 };
	{ // :skinning
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 compute_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/compute/bin/skinning.compute.spv"));
		pipeline_skinning = compute_pipeline_make(device, compute_bytecode);
		arena_scratch_end(scratch);
	}

	GFX_Pipeline pipeline_shadow = { 0 };
	{
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/shadow.vertex.spv"));
		String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/blank.fragment.spv"));

		pipeline_shadow = graphics_pipeline_make(device, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "pipeline_shadow",
			});
		arena_scratch_end(scratch);
	}

	GFX_Pipeline pipeline_3d = { 0 };
	GFX_Pipeline pipeline_skybox = { 0 };
	GFX_Pipeline pipeline_grass = { 0 };
	GFX_Pipeline pipeline_line3d = { 0 };
	{ // create 3d opaque graphics pipelines
		ArenaTemp scratch = arena_scratch_begin(NULL);
		String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/base.vertex.spv"));
		String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/phong.fragment.spv"));

		pipeline_3d = graphics_pipeline_make(device, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "spatial",
			  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
			  .color_attachment_count = 1,
			  .sample_count = SAMPLE_COUNT_8,
			  .cull_mode = CULL_MODE_BACK,
			});

		vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/grass.vertex.spv"));
		fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/grass.fragment.spv"));

		pipeline_grass = graphics_pipeline_make(device, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "pipeline_grass",
			  .color_attachment_count = 1,
			  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
			  .sample_count = SAMPLE_COUNT_8,
			});

		vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/skybox.vertex.spv"));
		fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/skybox.fragment.spv"));
		pipeline_skybox = graphics_pipeline_make(device, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "pipeline_skybox",
			  .color_attachment_count = 1,
			  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
			  .sample_count = SAMPLE_COUNT_8,
			});

		vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/line.vertex.spv"));
		fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/flat.fragment.spv"));
		pipeline_line3d = graphics_pipeline_make(device, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "line3d",
			  .color_attachment_count = 1,
			  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
			  .sample_count = SAMPLE_COUNT_8,
			});

		arena_scratch_end(scratch);
	}

	OS_Timestamp transparent_ts = os_file_last_modified(s("assets/shaders/fragment/bin/energyfield.fragment.spv"));
	GFX_Pipeline transparent = { 0 };
	{ // create 3d transparent pipelines
		ArenaTemp scratch = arena_scratch_begin(0);
		String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/base.vertex.spv"));
		String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/energyfield.fragment.spv"));

		transparent = graphics_pipeline_make(device, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "transparent",
			  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
			  .color_attachment_count = 1,
			  .sample_count = SAMPLE_COUNT_8,
			  .cull_mode = CULL_MODE_NONE,
			});

		arena_scratch_end(scratch);
	}

	GFX_Pipeline pipeline_2d = { 0 };
	GFX_Pipeline pipeline_line2d = { 0 };
	GFX_Pipeline pipeline_composite = { 0 };
	{
		ArenaTemp scratch = arena_scratch_begin(0);

		String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/batch2d.vertex.spv"));
		String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/quad.fragment.spv"));

		pipeline_2d = graphics_pipeline_make(device, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "canvas",
			  .color_attachments = { PIXEL_FORMAT_RGBA8_UNORM },
			  .color_attachment_count = 1,
			  .sample_count = SAMPLE_COUNT_1,
			  .cull_mode = CULL_MODE_BACK,
			  .disable_depth_test = true,
			  .disable_depth_write = true,

			  .enable_blend = true,
			  .src_color_factor = BLEND_FACTOR_SRC_ALPHA,
			  .dst_color_factor = BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			  .src_alpha_factor = BLEND_FACTOR_ONE,
			  .dst_alpha_factor = BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			});

		vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/line2d.vertex.spv"));
		fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/flat.fragment.spv"));
		pipeline_line2d = graphics_pipeline_make(device, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "line2d",
			  .color_attachments = { PIXEL_FORMAT_RGBA8_SRGB },
			  .color_attachment_count = 1,
			  .sample_count = SAMPLE_COUNT_8,
			  .cull_mode = CULL_MODE_BACK,
			  .disable_depth_test = true,
			  .disable_depth_write = true,

			});

		vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/fullscreen_quad.vertex.spv"));
		fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/composite.fragment.spv"));
		pipeline_composite = graphics_pipeline_make(device, vertex_bytecode, fragment_bytecode,
			(PipelineOptions){
			  .debug_name = "composite",
			  .color_attachments = { PIXEL_FORMAT_BGRA8_UNORM },
			  .color_attachment_count = 1,
			  .sample_count = SAMPLE_COUNT_1,
			  .cull_mode = CULL_MODE_BACK,
			  .disable_depth_test = true,
			  .disable_depth_write = true,
			});

		arena_scratch_end(scratch);
	}

	GFX_Buffer *geometry = gfx_buffer_make(device, MiB(256),
		(BufferOptions){
		  .debug_name = "geometry",
		  .memory = MEMORY_TYPE_GPU,
		  .usage = BUFFER_USAGE_STORAGE | BUFFER_USAGE_INDEX | BUFFER_USAGE_TRANSFER,
		});
	GFX_Buffer *grass_instancing_buffer = gfx_buffer_make(device, MiB(128),
		(BufferOptions){
		  .debug_name = "grass_instancing",
		  .memory = MEMORY_TYPE_GPU,
		  .usage = BUFFER_USAGE_STORAGE | BUFFER_USAGE_TRANSFER,
		});

	const uint32_t map_width = 32;
	const uint32_t map_depth = 32;

	Mesh meshes[MESH_MAX] = { 0 };
	meshes[MESH_TERRAIN_FLAT] = mesh_plane(permanent, plane_from_side(SIDE_UP), map_width, map_depth, map_width, map_depth);
	meshes[MESH_TERRAIN_FLAT].materials[0].textures[TEXTURE_SLOT_ALBEDO] = terrain_texture;

	meshes[MESH_TERRAIN_HEIGHTMAP] = mesh_heightmap(permanent, SIDE_TOP, 256.f, 256.f, noise_image);
	meshes[MESH_TERRAIN_HEIGHTMAP].materials[0].textures[TEXTURE_SLOT_ALBEDO] = terrain_texture;

	meshes[MESH_CYLINDER] = mesh_cylinder(permanent, unit3(UP), 1.0f, 0.5f, 0.5f, 32, 0, true, true);
	meshes[MESH_SPHERE] = mesh_sphere(permanent, unit3(UP), 1.0f, 32, 16);
	{
		ArenaTemp scratch = arena_scratch_begin(0);

		// Proportions in meters (Total length = 0.20m)
		float mult = 5.0f;
		float shaft_half_height = 0.075f * mult; // Total shaft length is 0.15m
		float shaft_radius = 0.005f * mult; // 5mm thick
		float head_height = 0.050f * mult; // 5cm tall cone
		float head_radius = 0.018f * mult; // 1.8cm base radius

		float3 cylinder_origin = { 0.0f, shaft_half_height, 0.0f };
		Mesh shaft = mesh_cylinder(
			scratch.arena,
			cylinder_origin,
			shaft_half_height,
			shaft_radius, shaft_radius,
			16, 1,
			false, true);

		float3 cone_origin = { 0.0f, shaft_half_height * 2.0f, 0.0f };
		Mesh head = mesh_cone(
			scratch.arena,
			cone_origin,
			head_height,
			head_radius,
			16);

		meshes[MESH_GIZMOS_ARROW] = mesh_merge(permanent, array_arg(Mesh, shaft, head));

		arena_scratch_end(scratch);
	}

	uint32_t animation_counts[MESH_MAX] = { 0 };
	AnimationClip *animations[MESH_MAX] = { 0 };
	(void)animations;

	for (uint32_t meshid = 0; meshid < MESH_MAX; ++meshid) {
		if (meshid_to_metadata[meshid].length == 0)
			continue;
		meshes[meshid] = load_gltf(permanent, meshid_to_metadata[meshid]);

		if (meshes[meshid].skeleton.bone_count == 0 || meshid == MESH_MAGE)
			continue;
		animations[meshid] = load_gltf_animations(permanent, meshid_to_metadata[meshid], &animation_counts[meshid]);
	}

	GFX_Command *cmd = gfx_transfer_cmd(device);
	if (cmd->handle) { // :upload
		gfx_cmd_buffer_barrier(cmd, RESOURCE_USAGE_UNDEFINED, RESOURCE_USAGE_TRANSFER_DST, 0, geometry->size, geometry);
		gfx_cmd_buffer_barrier(cmd, RESOURCE_USAGE_UNDEFINED, RESOURCE_USAGE_TRANSFER_DST, 0, grass_instancing_buffer->size, grass_instancing_buffer);

		for (uint32_t mesh_index = 0; mesh_index < countof(meshes); ++mesh_index) {
			Mesh *mesh = &meshes[mesh_index];

			for (uint32_t material_index = 0; material_index < mesh->material_count; ++material_index) {
				Material *material = &mesh->materials[material_index];

				for (uint32_t texture_slot = 0; texture_slot < TEXTURE_SLOT_COUNT; ++texture_slot) {
					Image2D *img = &material->textures[texture_slot];
					uint32_t mip_level = 0;

					if (img->pixels) {
						String8 head = str8_filename(meshid_to_metadata[mesh_index]);
						String8 tail = texture_slot_to_string[texture_slot];

						head = head.length ? head : s("gen");

						String8 name = { .length = head.length + tail.length + 1 };
						name.text = arena_push_count(permanent, uint8_t, name.length + 1);

						uint32_t cursor = 0;

						memory_copy(name.text + cursor, head.text, head.length);
						cursor += head.length;

						name.text[cursor] = ':';
						cursor += 1;

						memory_copy(name.text + cursor, tail.text, tail.length);
						cursor += tail.length;

						img->handle = gfx_image_make(device, img->width, img->height,
							(ImageOptions){
							  .debug_name = (char *)name.text,
							  .pixels = img->pixels,
							  .format = img->format,
							  .max_mip_level = mip_level,
							});
					} else {
						img->width = img->height = 1;
						img->format = PIXEL_FORMAT_RGBA8_SRGB;
						img->handle = white_texture;
					}
				}
			}
		}

		uint64_t grass_upload_offset = arena_mark(cmd->transient_arena);
		// vertex_count = 256 * 256 * 3 * 6 = 1.179.648
		for (uint32_t z = 0; z < map_depth; ++z) {
			for (uint32_t x = 0; x < map_width; ++x) {
				float3 pos = {
					.x = x - (map_width * 0.5f) + randf_range(0.0, 1.0),
					.y = 0.0f,
					.z = z - (map_depth * 0.5f) + randf_range(0.0, 1.0),
				};
				pos = scale3(pos, 1.f / 2.f);
				*arena_push_count(cmd->transient_arena, float4x4, 1) = mul4x4(make4x4_from_rotation(unit3(UP), randf_range(0, TAU)), make4x4_from_translation(pos));
			}
		}
		gfx_cmd_buffer_to_buffer(cmd, grass_instancing_buffer, cmd->transient_buffer, 0, grass_upload_offset, sizeof(float4x4) * map_width * map_depth);
		gfx_cmd_buffer_barrier(cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_SHADER_READ, 0, sizeof(float4x4) * map_width * map_depth, grass_instancing_buffer);

		uint64_t transient_upload_start_offset = cmd->transient_arena->offset;
		uint64_t geometry_upload_cursor = 0;

		for (uint32_t mesh_index = 0; mesh_index < countof(meshes); ++mesh_index) {
			Mesh *mesh = &meshes[mesh_index];
			mesh->buffer = geometry;

			uint64_t total_vertex_buffer_size = alignup(mesh->total_vertex_count * sizeof(Vertex3D), 256);
			uint64_t total_index_buffer_size = alignup(mesh->total_index_count * sizeof(uint32_t), 256);
			uint64_t total_skinning_buffer_size = alignup(mesh->total_vertex_count * sizeof(SkinningVertex3D), 256);

			// Vertices
			mesh->buffer_vertex_byte_offset = geometry_upload_cursor;
			memory_copy(arena_push(cmd->transient_arena, total_vertex_buffer_size, 1, false), mesh->vertices, mesh->total_vertex_count * sizeof(Vertex3D));
			geometry_upload_cursor += total_vertex_buffer_size;

			// Indices
			mesh->buffer_index_byte_offset = geometry_upload_cursor;
			memory_copy(arena_push(cmd->transient_arena, total_index_buffer_size, 1, false), mesh->indices, mesh->total_index_count * sizeof(uint32_t));
			geometry_upload_cursor += total_index_buffer_size;

			// Skinning
			if (mesh->skeleton.bone_count) {
				mesh->buffer_skinning_data_byte_offset = geometry_upload_cursor;
				memory_copy(arena_push(cmd->transient_arena, total_skinning_buffer_size, 1, false), mesh->skinning, mesh->total_vertex_count * sizeof(SkinningVertex3D));
				geometry_upload_cursor += total_skinning_buffer_size;
			}
		}

		gfx_cmd_buffer_to_buffer(cmd, geometry, cmd->transient_buffer, 0, transient_upload_start_offset, geometry_upload_cursor);

		gfx_cmd_buffer_barrier(cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_SHADER_READ, 0, geometry_upload_cursor, geometry);
		gfx_cmd_buffer_barrier(cmd, RESOURCE_USAGE_TRANSFER_DST, RESOURCE_USAGE_INDEX_BUFFER, 0, geometry_upload_cursor, geometry);
	}

	float dt = 0.0f;
	float last_frame = 0.0f;

	Arena frame_arena[] = { arena_make(MiB(4)) };
	IMGUI_Context imgui = { 0 };

	// :init
	float2 compute_mouse = { 0 };
	typedef enum {
		VIEWPORT_STATE_EDITOR,
		VIEWPORT_STATE_GAME,

		VIEWPORT_STATE_COUNT,
	} ViewportState;

	ViewportState state = VIEWPORT_STATE_EDITOR;

	Camera3D cameras[VIEWPORT_STATE_COUNT] = {
		[VIEWPORT_STATE_EDITOR] = {
		  .projection = CAMERA_PROJECTION_PERSPECTIVE,
		  .position = { 0.0f, 1.5f, 20.f },
		  .target = { 0.0f, 1.5f, 0.0f },
		  .up = unit3(UP),
		  .fovy = 45.f,
		},
		[VIEWPORT_STATE_GAME] = {
		  .projection = CAMERA_PROJECTION_PERSPECTIVE,
		  .position = { 0.0f, 1.5f, 20.f },
		  .target = { 0.0f, 1.5f, 0.0f },
		  .up = unit3(UP),
		  .fovy = 45.f,
		},
	};

	// :environment
	typedef struct {
		float4 position;
		float4 color;
		float4x4 matrix;
	} Light;

	Light lights[] = {
		{ .position = { 0.0f, 20.0f, -30.0f, 1.0f }, (float4){ 1.0f, 1.0f, 1.0f, 1.0f }, identity4x4() }, // day
		{ .position = { 0.0f, 20.0f, -30.0f, 1.0f }, (float4){ 1.0f, 0.5f, 0.2f, 1.0f }, identity4x4() }, // sunset
		{ .position = { 0.0f, 20.0f, -30.0f, 1.0f }, (float4){ 0.05f, 0.15f, 0.6f, 1.0f }, identity4x4() }, // night
	};

	bool use_heightmap = false;
	float ambient_strength = 0.2f;
	float fog_density = 0.02f, fog_gradient = 5.0f;
	bool draw_collision_shapes = true, draw_grass = true, draw_skybox = true;
	uint32_t light_index = 0;

	World scenes[] = {
		{ .entity_count = 1 },
		{ .entity_count = 1 },
	};

	World *scene = scenes + 0;
	{
		ArenaTemp scratch = arena_scratch_begin(0);
		JSON_Node *root = json_parse_file(scratch.arena, s("assets/data/test.scene"));
		json_to_world(root, scene);
		json_to_world(root, scenes + 1);
		arena_scratch_end(scratch);
	}

	bool is_open = true;
	while (is_open) {
		double time = os_time_ns() * 1e-9 - start_time * 1e-9;
		dt = time - last_frame;
		last_frame = time;

		input_update();
		OS_Event event = { 0 };
		while (os_event_poll(&event)) {
			switch (event.type) {
				case OS_EVENT_TYPE_SURFACE_CLOSE:
					if (event.surface == main_render)
						is_open = false;
					else
						os_surface_close(popup_compute);
					break;

				case OS_EVENT_TYPE_SURFACE_RESIZE: {
					uint2 dims = { event.as.resize.width, event.as.resize.height };
					if (event.surface == main_render) {
						gfx_swapchain_resize(device, main_swapchain, dims.x, dims.y);
						gfx_image_resize(device, depthbuffer, dims.x, dims.y);
						gfx_image_resize(device, msaa_target, dims.x, dims.y);
						gfx_image_resize(device, spatial_target, dims.x, dims.y);
						gfx_image_resize(device, ui_target, dims.x, dims.y);

					} else {
						gfx_swapchain_resize(device, popup_swapchain, dims.x, dims.y);
						gfx_image_resize(device, compute_image, dims.x, dims.y);
					}
				} break;

				case OS_EVENT_TYPE_KEY_PRESS:
				case OS_EVENT_TYPE_KEY_RELEASE:
					input_feed_key(event.as.key.key_code, event.type == OS_EVENT_TYPE_KEY_PRESS);
					break;

				case OS_EVENT_TYPE_MOUSE_PRESS:
				case OS_EVENT_TYPE_MOUSE_RELEASE:
					if (event.surface == main_render)
						input_feed_mouse_button(event.as.mouse_button.button, event.type == OS_EVENT_TYPE_MOUSE_PRESS);
					break;

				case OS_EVENT_TYPE_MOUSE_MOVE:
					if (event.surface == main_render)
						input_feed_mouse_motion((double)event.as.mouse_move.x, (double)event.as.mouse_move.y);
					else {
						compute_mouse.x = event.as.mouse_move.x;
						compute_mouse.y = event.as.mouse_move.y;
					}
					break;
				default:
					break;
			}
		}

		// :update
		Arena batch_2d[] = { {
		  .base = arena_push_count(frame_arena, QuadVertex2D, 6 * 1024),
		  .capacity = sizeof(QuadVertex2D) * 6 * 1024,
		} };
		Arena batch_line3[] = { {
		  .base = arena_push_count(frame_arena, LineVertex3D, 6 * 2048),
		  .capacity = sizeof(LineVertex3D) * 6 * 2048,
		} };
		Arena batch_line2d[] = {
			{
			  .base = arena_push_count(frame_arena, LineVertex3D, 6 * 1024),
			  .capacity = sizeof(LineVertex3D) * 6 * 1024,
			}
		};

		uint2 dims = os_surface_size(main_render);
		float2 mouse_delta = cast2(input_mouse_delta(), float2);
		float2 mouse = cast2(input_mouse_position(), float2);
		Rectangle viewport = { 0.0f, 0.0f, dims.x, dims.y };
		mouse_delta.x /= dims.x;
		mouse_delta.y /= dims.y;

		if (input_key_pressed(KEY_CODE_TAB))
			state = (state + 1) % VIEWPORT_STATE_COUNT;

		Camera3D *camera = &cameras[state];
		static uint32_t triangle_step = 0;
		if (input_key_pressed(KEY_CODE_N))
			triangle_step = (triangle_step + 1) % (meshes[MESH_CYLINDER].total_index_count / 3);

		float4x4 view = lookat(camera->position, camera->target, camera->up);
		float4x4 proj = perspective(deg_to_rad(45.f), (float)dims.x / (float)dims.y, 0.1f, 500.f);

		imgui_frame_begin(&imgui, dt);
		imgui.mouse.last_position = imgui.mouse.position;
		imgui.mouse.position = mouse;
		imgui.default_font = fonts + FONT_BAKE_SIZE_16;
		for (uint32_t index = 0; index < 3; ++index) {
			imgui.mouse.pressed[index] = input_mouse_pressed(index);
			imgui.mouse.released[index] = input_mouse_released(index);
		}

		switch (state) {
			case VIEWPORT_STATE_EDITOR: {
				if (input_mouse_down(MOUSE_BUTTON_MIDDLE))
					os_cursor_capture(main_render, true);
				else
					os_cursor_capture(main_render, false);

				scene_camera_orbit(camera, mouse_delta);

				for (uint32_t index = 0; index < scene->entity_count; ++index) {
					Entity *entity = &scene->entities[index];
					if (entity_has(entity, ENTITY_FEATURE_ANIMATE)) {
						uint32_t bone_count = meshes[entity->meshid].skeleton.bone_count;
						entity->skin_matrices = arena_push_count(frame_arena, float4x4, bone_count);
						for (uint32_t bone_index = 0; bone_index < bone_count; ++bone_index) {
							entity->skin_matrices[bone_index] = identity4x4();
						}
					}
				}

				// :editor
				{
					static float2 panel_offset = { 0 };
					static float2 mouse_grab_offset = { 0 };
					static IMGUI_Dock panel_dock = IMGUI_DOCK_NONE;

					IMGUI_Widget *panel = imgui_widget_ex(__LINE__,
						(IMGUI_Style){
						  .flow = IMGUI_VERTICAL,
						  .mode = {
							panel_dock == IMGUI_DOCK_CENTER || panel_dock == IMGUI_DOCK_TOP || panel_dock == IMGUI_DOCK_BOTTOM ? IMGUI_MODE_GROW : IMGUI_MODE_FIXED,
							panel_dock == IMGUI_DOCK_CENTER || panel_dock == IMGUI_DOCK_LEFT || panel_dock == IMGUI_DOCK_RIGHT ? IMGUI_MODE_GROW : IMGUI_MODE_FIXED,
						  },
						  .bg = RED,
						});
					panel->offset[0] = panel_offset.x, panel->offset[1] = panel_offset.y;
					panel->size[0] = 350.f, panel->size[1] = 500.f;

					IMGUI_Widget *root = imgui_widget_ex(__LINE__,
						(IMGUI_Style){
						  .mode = { IMGUI_MODE_FIXED, IMGUI_MODE_FIXED },
						});
					root->size[0] = viewport.width, root->size[1] = viewport.height;

					if (panel_dock) {
						if (panel_dock == IMGUI_DOCK_CENTER)
							imgui_parent(panel, root);
						else {
							IMGUI_Widget *container = imgui_widget_ex(__LINE__,
								(IMGUI_Style){
								  .flow = panel_dock == IMGUI_DOCK_TOP || panel_dock == IMGUI_DOCK_BOTTOM ? IMGUI_VERTICAL : IMGUI_HORIZONTAL,
								  .mode = { IMGUI_MODE_GROW, IMGUI_MODE_GROW },
								});
							imgui_parent(container, root);

							uint32_t dock_index = panel_dock < IMGUI_DOCK_CENTER ? 0 : 2;
							for (uint32_t index = 0; index < 3; ++index) {
								IMGUI_Widget *division = imgui_widget_ex(0,
									(IMGUI_Style){
									  .mode = { IMGUI_MODE_GROW, IMGUI_MODE_GROW },
									  .align = { index, index },
									});
								imgui_parent(division, container);

								if (dock_index == index)
									imgui_parent(panel, division);
							}
						}
					}

					imgui_push_parent(root);
					IMGUI_Widget *topbar = imgui_widget_ex(__LINE__,
						(IMGUI_Style){
						  .mode = { IMGUI_MODE_GROW, IMGUI_MODE_FIT },
						  .p = 4.0f,
						  .gap = 1.0f,
						  .bg = hex(0x151b23),
						});

					imgui_push_parent(topbar);
					{ // topbar
						IMGUI_Style topbar_btn = {
							.mode = { 0, IMGUI_MODE_GROW },
							.align = { 0, IMGUI_ALIGN_CENTER },
							.ph = 8.0f,
							.pv = 0.0f,
							.bg = hex(0x262c36),
							.fg = WHITE,
							.border_radius = 4.0f
						};

						imgui_push_style(topbar_btn);
						static bool toggle_file_menu = false;
						if (imgui_button_label(s("File")).pressed) { toggle_file_menu = !toggle_file_menu; }
						if (imgui_button_label(s("Edit")).pressed) { LOG_INFO("Edit"); }
						if (imgui_button_label(s("Help")).pressed) { LOG_INFO("Help"); }
						imgui_spacer();
						if (imgui_button_image(&icons[ICON_PLAY], 0.5f).released) { LOG_INFO("Play"); }
						if (imgui_button_image(&icons[ICON_PAUSE], 0.5f).released) { LOG_INFO("Pause"); }
						if (imgui_button_image(&icons[ICON_STOP], 0.5f).released) { LOG_INFO("Stop"); }
						imgui_pop_style();

						imgui_pop_parent();
					}

					IMGUI_Widget *body = imgui_widget_ex(__LINE__,
						(IMGUI_Style){
						  .mode = { IMGUI_MODE_GROW, IMGUI_MODE_GROW },
						  .p = 12.0f,
						  .gap = 12.0f,
						  .bg = hex(0x0d1117),
						});

					imgui_push_parent(body);
					{ // body
						IMGUI_Widget *track = imgui_widget_ex(__LINE__,
							(IMGUI_Style){
							  .mode = { IMGUI_MODE_GROW, IMGUI_MODE_FIXED },
							  .bg = RED,
							});
						track->size[1] = imgui.default_font->bake_size;

						IMGUI_Widget *thumb = imgui_widget_ex(__LINE__,
							(IMGUI_Style){
							  .mode = { IMGUI_MODE_FIXED, IMGUI_MODE_GROW },
							  .bg = BLUE,
							});
						imgui_parent(thumb, track);
						thumb->size[0] = imgui.default_font->bake_size;

						Rectangle track_rect = imgui_rect_cached(track);
						Rectangle thumb_rect = imgui_rect_cached(thumb);
						float travel = track_rect.width - thumb_rect.width;

						float min = 0.0f, max = 10.0f;
						static float t = 0.0f;

						IMGUI_Interact interct = imgui_interact(track->id, track_rect);
						if (interct.held) {
							float2 mouse = imgui.mouse.position;
							mouse.x -= track_rect.x + thumb_rect.width * 0.5f;
							mouse.x = clampf(mouse.x, 0.0f, travel);

							float mouse_ratio = (travel != 0.0f) ? (mouse.x / travel) : 0.0f;
							t = min + (mouse_ratio * (max - min));
						}

						float t_norm = max - min != 0.0f ? (t - min) / (max - min) : 0.0f;
						thumb->offset[0] = t_norm * travel;

						imgui_pop_parent();
					}

					imgui_pop_parent();

					imgui_parent(topbar, panel);
					imgui_parent(body, panel);

					IMGUI_Interact topbar_interact = imgui_interact(topbar->id, imgui_rect_cached(topbar));
					if (panel_dock) {
						if (topbar_interact.double_release || (topbar_interact.held && rect_contains_point(imgui_rect_cached(topbar), mouse) == false)) {
							panel_dock = IMGUI_DOCK_NONE;
							topbar_interact.double_release = false;
							topbar_interact.released = false;
							topbar_interact.held = false;

							Rectangle rect = imgui_rect_cached(topbar);
							float2 offset = sub2(mouse, make2(rect.x, rect.y));
							panel_offset = sub2(mouse, mouse_grab_offset);
						}
					}

					if (panel_dock == IMGUI_DOCK_NONE) {
						if (topbar_interact.pressed)
							mouse_grab_offset = sub2(mouse, panel_offset);
						if (topbar_interact.held)
							panel_offset = sub2(mouse, mouse_grab_offset);

						Rectangle dock_rect = rect_from_center(scale2(cast2(dims, float2), 0.5f), make2(40.0f, 40.0f));

						float EDGE_MARGIN = 10.0f;
						int2 dock_dir = {
							(topbar_interact.held && imgui.mouse.position.x >= viewport.width - EDGE_MARGIN) - (topbar_interact.held && imgui.mouse.position.x <= EDGE_MARGIN),
							(topbar_interact.held && imgui.mouse.position.y >= viewport.height - EDGE_MARGIN) - (topbar_interact.held && imgui.mouse.position.y <= EDGE_MARGIN)
						};

						bool dock =
							topbar_interact.double_release ||
							(topbar_interact.held && rect_contains_point(dock_rect, mouse)) ||
							dock_dir.x != 0 || dock_dir.y != 0;

						if (dock) {
							IMGUI_Dock dock_orientation = 0;

							if (dock_dir.x == 0 && dock_dir.y == 0) {
								dock_orientation = IMGUI_DOCK_CENTER;

								root->settings.align[0] = IMGUI_ALIGN_CENTER, root->settings.align[1] = IMGUI_ALIGN_CENTER;
								IMGUI_Widget *dock_preview = imgui_widget_ex(__LINE__,
									(IMGUI_Style){
									  .mode = { IMGUI_MODE_GROW, IMGUI_MODE_GROW },
									  .bg = rgba(0, 128, 128, 48),
									});
								imgui_parent(dock_preview, root);
							} else {
								dock_orientation = dock_dir.x ? dock_dir.x < 0 ? IMGUI_DOCK_LEFT : IMGUI_DOCK_RIGHT : dock_dir.y < 0 ? IMGUI_DOCK_TOP
																																	 : IMGUI_DOCK_BOTTOM;
								IMGUI_Widget *container = imgui_widget_ex(__LINE__,
									(IMGUI_Style){
									  .flow = dock_dir.x ? IMGUI_HORIZONTAL : IMGUI_VERTICAL,
									  .mode = { IMGUI_MODE_GROW, IMGUI_MODE_GROW },
									});
								imgui_parent(container, root);

								IMGUI_Widget *divisions[3] = { 0 };

								uint32_t preview_index = dock_orientation < IMGUI_DOCK_CENTER ? 0 : 2;
								for (uint32_t index = 0; index < 3; ++index) {
									divisions[index] = imgui_widget_ex(0, (IMGUI_Style){ .mode = { IMGUI_MODE_GROW, IMGUI_MODE_GROW } });
									imgui_parent(divisions[index], container);

									if (preview_index == index)
										divisions[index]->settings.bg = rgba(0, 128, 128, 96);
								}
							}

							if (imgui.mouse.released[0]) {
								panel_offset.x = 0.0f, panel_offset.y = 0.0f;
								panel_dock = dock_orientation;
							}
						}

						if (panel_dock == IMGUI_DOCK_NONE)
							write2(panel_offset, panel->offset);

						if (panel->parent == 0) {
							imgui_fit_tree(panel);
							imgui_grow_tree(panel);
							imgui_position_tree(panel);
						}
					}

					imgui_fit_tree(root);
					imgui_grow_tree(root);
					imgui_position_tree(root);
				}
			} break;
			case VIEWPORT_STATE_GAME: {
				os_cursor_capture(main_render, input_key_pressed(KEY_CODE_E) ? !os_cursor_captured(main_render) : os_cursor_captured(main_render));

				if (0)
					for (uint32_t index = 0; index < scene->entity_count; ++index) { // :heightmap
						Entity *entity = &scene->entities[index];
						if (entity_has(entity, ENTITY_FEATURE_DRAW_MESH) == false)
							continue;

						if (use_heightmap) {
							float px = clampf(entity->transform.translation.x + map_width * 0.5f, 0.0f, map_width);
							float pz = clampf(entity->transform.translation.z + map_depth * 0.5f, 0.0f, map_depth);

							int32_t x0 = (int32_t)px;
							int32_t z0 = (int32_t)pz;

							int32_t x1 = MIN(x0 + 1, (int32_t)noise_image.width - 1);
							int32_t z1 = MIN(z0 + 1, (int32_t)noise_image.height - 1);

							float hs[4] = {
								[0] = (noise_image.pixels[(x0 + z0 * noise_image.width) * 4] / 255.f) - 0.5f,
								[1] = (noise_image.pixels[(x1 + z0 * noise_image.width) * 4] / 255.f) - 0.5f,
								[2] = (noise_image.pixels[(x0 + z1 * noise_image.width) * 4] / 255.f) - 0.5f,
								[3] = (noise_image.pixels[(x1 + z1 * noise_image.width) * 4] / 255.f) - 0.5f,
							};
							float u = px - (float)x0;
							float v = pz - (float)z0;

							float top_edge = hs[0] + u * (hs[1] - hs[0]);
							float bottom_edge = hs[2] + u * (hs[3] - hs[2]);

							float height = top_edge + v * (bottom_edge - top_edge);
							/* float hs = (heightmap.pixels[(x0 + z0 * heightmap.width) * 4] / 255.f) - 0.5f; */
							/* tranform->translation.y = hs * 40.f; */
							entity->transform.translation.y = height * 40.f;
						} else
							entity->transform.translation.y = 0.0f;
					}

				for (uint32_t index = 0; index < scene->entity_count; ++index) {
					Entity *entity = &scene->entities[index];

					float2 input_vector = { 0 };
					float3 velocity = { 0 };
					float3 direction = { 0 };

					// :player
					if (entity_has(entity, ENTITY_FEATURE_PLAYER_CONTROLLED)) {
						input_vector = (float2){
							.x = input_key_down(KEY_CODE_W) - input_key_down(KEY_CODE_S),
							.y = input_key_down(KEY_CODE_D) - input_key_down(KEY_CODE_A),
						};

						if (dot2(input_vector, input_vector) > 0) {
							float3 camera_offset = sub3(camera->position, entity->transform.translation);
							float r = len3(camera_offset);
							if (r < EPSILON)
								r = EPSILON;

							float current_theta = acosf(camera_offset.y / r);
							float current_azimuth = atan2f(camera_offset.z, camera_offset.x); // [-pi, pi]
																							  //
							float target_angle = -current_azimuth - PIf + atan2f(input_vector.x, input_vector.y);
							quat4 target_rotation = quat4_from_axis_angle(unit3(UP), target_angle);

							float t = 1.0f - expf(-15.0f * dt);
							entity->transform.rotation = quat4_slerp(entity->transform.rotation, target_rotation, t);
						}

						const float walk_speed = 3.0f, run_speed = 6.0f;

						float3 camera_position = camera->position;
						float3 camera_target = camera->target;

						camera_position.y = 0.0f;
						camera_target.y = 0.0f;

						float3 forward, right;

						forward = norm3(sub3(camera_target, camera_position));

						right = cross3(forward, camera->up);
						right = norm3(right);

						direction = add3(direction, scale3(forward, input_vector.x));
						direction = add3(direction, scale3(right, input_vector.y));

						float length = len3(direction);
						if (length > EPSILON)
							direction = scale3(direction, 1 / length);
						entity->move_speed = input_key_down(KEY_CODE_LEFTSHIFT) ? run_speed : walk_speed;

						velocity = scale3(direction, entity->move_speed * dt);
						/* velocity = add3(velocity, (float3){ 0.0f, -0.1f, 0.0f }); */
					}

					// :ai
					if (entity->target && entity_has(entity, ENTITY_FEATURE_FOLLOW_TARGET)) {
						Entity *target = entity->target;
						float3 delta = sub3(target->transform.translation, entity->transform.translation);
						float3 direction = norm3(delta);

						entity->transform.rotation = quat4_from_axis_angle(unit3(UP), atan2f(direction.x, direction.z));

						entity->move_speed = 3.0f;

						input_vector = (float2){ direction.x, direction.z };
						velocity = scale3(direction, entity->move_speed * dt);
					}

					// :collision
					if (entity_has(entity, ENTITY_FEATURE_COLLIDABLE) && entity->shape.kind == SHAPE_KIND_AABB3) {
						for (uint32_t iteration = 0; iteration < 6; ++iteration) {
							if (lensq3(velocity) <= EPSILON)
								break;
							CastResult3 nearest = CAST3_NO_HIT;
							Entity *nearest_entity = 0;

							Ray3 r = { add3(aabb3_center(entity->shape.as.aabb3), entity->transform.translation), velocity };
							for (uint32_t entity_index = 0; entity_index < scene->entity_count; ++entity_index) {
								Entity *other = &scene->entities[entity_index];
								if (other == entity || entity_has(other, ENTITY_FEATURE_COLLIDABLE) == false || other->shape.kind != SHAPE_KIND_AABB3)
									continue;

								float3 center = add3(aabb3_center(other->shape.as.aabb3), other->transform.translation);
								// NOTE: Minkowski sum
								float3 half_extent = mul3(other->transform.scale, aabb3_half_extent(other->shape.as.aabb3));
								float3 swept_extent = add3(
									mul3(other->transform.scale, aabb3_half_extent(other->shape.as.aabb3)),
									mul3(entity->transform.scale, aabb3_half_extent(entity->shape.as.aabb3)));

								CastResult3 result = raycast_aabb3(r, aabb3_from_center(center, swept_extent));
								if (result.t < nearest.t) {
									nearest = result;
									nearest_entity = other;
								}
							}

							bool overlap_before_move = check_for_overlap(entity, scene->entities, scene->entity_count);
							if (nearest.hit == false) {
								if (overlap_before_move == false)
									if (check_for_overlap(entity, scene->entities, scene->entity_count))
										LOG_ERROR("error in hit detection.");

								entity->transform.translation = add3(entity->transform.translation, velocity);
								break;
							}

							// Collision resolution
							float t_min = maxf(minf(nearest.t, 1.0f), 0.0f);
							if (t_min <= EPSILON)
								break;

							float3 position_before_move = entity->transform.translation;

							entity->transform.translation = add3(entity->transform.translation, scale3(velocity, t_min));
							entity->transform.translation = add3(entity->transform.translation, scale3(nearest.normal, 0.001f));

							if (overlap_before_move == false)
								if (check_for_overlap(entity, scene->entities, scene->entity_count)) {
									LOG_ERROR("error in t. from { %.2f, %.2f, %2f } to { %.2f, %.2f, %.2f }", spread3(position_before_move), spread3(entity->transform.translation));
									if (nearest_entity) {
										LOG_DEBUG(
											"\nsafe_margin = %.2f\n"
											"final_t = %.2f\n"
											"contact_point= { %.2f, %.2f, %2f }\n"
											"contact_normal = { %.2f, %.2f, %.2f }\n"
											"entity[%d] = {\n"
											"  velocity = { %.2f, %.2f, %.2f }\n"
											"  translation = { %.2f, %.2f, %.2f }\n"
											"  aabb = {\n    center = { %.2f, %.2f, %.2f },\n    half_extent = { %.2f, %.2f, %.2f }\n  }\n"
											"}\n"
											"other[%d] = {\n"
											"  translation = { %.2f, %.2f, %.2f }\n"
											"  aabb = {\n    center = { %.2f, %.2f, %.2f },\n    half_extent = { %.2f, %.2f, %.2f }\n  }\n"
											"}\n",
											nearest.t,
											t_min,
											spread3(nearest.point),
											spread3(nearest.normal),
											indexof(scene->entities, entity),
											spread3(velocity),
											spread3(entity->transform.translation),
											spread3(aabb3_center(entity->shape.as.aabb3)),
											spread3(aabb3_half_extent(entity->shape.as.aabb3)),
											indexof(scene->entities, nearest_entity),
											spread3(nearest_entity->transform.translation),
											spread3(aabb3_center(nearest_entity->shape.as.aabb3)),
											spread3(aabb3_half_extent(nearest_entity->shape.as.aabb3)));
									}
								}

							velocity = sub3(velocity, scale3(nearest.normal, dot3(velocity, nearest.normal)));
							velocity = scale3(velocity, 1.0f - t_min);

							draw3_arrow(batch_line3, nearest.point, add3(nearest.point, scale3(nearest.normal, 0.8f)), 3.0f, RED, view, proj, dims.x);
						}
					}

					if (
						entity_has(entity, ENTITY_FEATURE_ANIMATE) &&
						entity_has(entity, ENTITY_FEATURE_DRAW_MESH) &&
						animation_counts[entity->meshid] // :skeletal
					) {
						Pose final = anim_pose_sample(
							frame_arena,
							&animations[entity->meshid][entity->current_anim],
							fmodf(entity->anim_t, animations[entity->meshid][entity->current_anim].duration) //
						);

						uint32_t target_anim = entity->current_anim;
						if (dot2(input_vector, input_vector)) {
							input_vector = norm2(input_vector);

							if (input_key_down(KEY_CODE_LEFTSHIFT))
								target_anim = find_animation(animations[entity->meshid], animation_counts[entity->meshid], s("freehand/run-loop"));
							else
								target_anim = find_animation(animations[entity->meshid], animation_counts[entity->meshid], s("freehand/walk-loop"));
						} else
							target_anim = find_animation(animations[entity->meshid], animation_counts[entity->meshid], s("freehand/idle-loop"));

						if (entity->current_anim != target_anim) {
							Pose start = final;
							Pose end = anim_pose_sample(frame_arena, &animations[entity->meshid][target_anim], fmodf(entity->anim_t, animations[entity->meshid][target_anim].duration));

							final = anim_pose_blend_local(frame_arena, &end, &start, entity->blend_t, 0);

							entity->blend_t += dt * 5;
							if (entity->blend_t >= 1.0f) {
								entity->current_anim = target_anim;
								entity->blend_t = 0.0f;
							}
						}

						Mesh *mesh = &meshes[entity->meshid];
						entity->skin_matrices = anim_pose_skinning_matrices(frame_arena, anim_pose_local_to_model(frame_arena, &final, &mesh->skeleton), &mesh->skeleton);

						entity->anim_t += dt;
					}
				}

				Entity *player = 0;
				for (uint32_t index = 0; index < scene->entity_count; ++index) {
					Entity *e = &scene->entities[index];
					if (entity_has(e, ENTITY_FEATURE_PLAYER_CONTROLLED)) {
						player = e;
						break;
					}
				}

				if (player) { // :camera
					static float azimuth = PIf * 3 / 2.f;
					static float theta = PIf / 3.f;
					static const float sensitivity = 1.0f;
					static const float spring_arm_length = 10.f;

					float yaw_delta = mouse_delta.x * sensitivity;
					float pitch_delta = mouse_delta.y * sensitivity;

					azimuth = fmodf(azimuth + yaw_delta, PIf * 2.f);
					if (azimuth < 0)
						azimuth += PIf * 2.f;

					theta = clampf(theta - pitch_delta, PIf / 4.f, PIf / 2.f);

					float3 camera_offset = sub3(camera->position, player->transform.translation);

					float r = len3(camera_offset);
					if (r < EPSILON)
						r = EPSILON;

					float current_theta = acosf(camera_offset.y / r);
					float current_azimuth = atan2f(camera_offset.z, camera_offset.x); // [-pi, pi]

					if (current_azimuth < 0)
						current_azimuth += PIf * 2.f;

					float da = azimuth - current_azimuth;
					if (da > PIf)
						da -= PIf * 2.f;
					if (da < -PIf)
						da += PIf * 2.f;

					float t = 1.0f - expf(-15.0f * dt);

					current_azimuth += t * da;
					current_theta += t * (theta - current_theta);

					camera->position = (float3){
						(spring_arm_length * sinf(current_theta) * cosf(current_azimuth)) + player->transform.translation.x,
						spring_arm_length * cosf(current_theta),
						(spring_arm_length * sinf(current_theta) * sinf(current_azimuth)) + player->transform.translation.z,
					};

					camera->target.x = player->transform.translation.x;
					camera->target.z = player->transform.translation.z;

					camera->position.y += player->transform.translation.y;
					camera->target.y = player->transform.translation.y + 1.5f;
				}

				if (player) { // :interact
					Entity *target = 0;
					float closest = FLOAT_MAX;

					if (input_key_pressed(KEY_CODE_F)) {
						for (uint32_t entity_index = 0; entity_index < scene->entity_count; ++entity_index) {
							Entity *entity = &scene->entities[entity_index];
							if (entity_has(entity, ENTITY_FEATURE_INTERACTABLE) == false || entity == player)
								continue;

							float3 offset = sub3(entity->transform.translation, player->transform.translation);
							float dist_sq = dot3(offset, offset);

							if (dist_sq <= (entity->interact_radius * entity->interact_radius) && dist_sq < closest) {
								closest = dist_sq;
								target = entity;
							}
						}

						if (target)
							LOG_INFO("interaction with entity %u (%s)!", indexof(scene->entities, target), str8_filename(meshid_to_metadata[target->meshid]).text);
					}
				}

			} break;
			default:
				break;
		}

		for (uint32_t index = 0; index < imgui.widget_count; ++index) {
			IMGUI_Widget *widget = &imgui.widgets[index];
			if (imgui_valid(widget)) {
				IMGUI_Widget *parent = &imgui.widgets[widget->parent];
				// TODO: Scissor
				/* if (imgui_valid(parent)) */
				/* 	BeginScissorMode(parent->offset[0], parent->offset[1], parent->size[0], parent->size[1]); */

				if (widget->settings.image) {
					draw2d_quad(
						batch_2d,
						imgui_rect_live(widget),
						image_rect(*widget->settings.image),
						widget->settings.image,
						(float2){ 0 },
						0.0f,
						0.0f, TRANSPARENT, splat4(widget->settings.border_radius), widget->settings.fg);
				} else if (widget->settings.text.length) {
					/* draw2d_rect(batch_2d, imgui_widget_rect(widget), ORANGE); */
					draw2d_textf(batch_2d, fonts + FONT_BAKE_SIZE_16, wrap2(widget->offset), widget->settings.fg, widget->settings.text);
				} else {
					draw2d_rect_rounded(batch_2d, imgui_rect_live(widget), splat4(widget->settings.border_radius), widget->settings.bg);
				}
				/* if (imgui_valid(parent)) */
				/* 	EndScissorMode(); */
			}
		}
		imgui_frame_end();

		/* Font *font = &fonts[FONT_BAKE_SIZE_16]; */
		/* draw2d_textf(batch_2d, font, gfx_image_id(context, font->atlas.handle), make2(dims.x - 60.0f, 20.0f), WHITE, */
		/* 	s("FPS %d"), (uint32_t)(1.0 / dt)); */

		if (draw_collision_shapes) {
			for (uint32_t instance_index = 0; instance_index < scene->entity_count; ++instance_index) {
				Entity *entity = &scene->entities[instance_index];
				if (entity_has(entity, ENTITY_FEATURE_COLLIDABLE) == false)
					continue;

				Shape3 a = { 0 };
				if (entity->shape.kind == SHAPE_KIND_AABB3)
					a = shape3_from_aabb3(aabb3_from_center(aabb3_center(entity->shape.as.aabb3), mul3(aabb3_half_extent(entity->shape.as.aabb3), entity->transform.scale)));

				draw3_shape_outline(batch_line3, entity->shape.kind == SHAPE_KIND_AABB3 ? &a : &entity->shape, entity->transform.translation, 3.0f);
			}
		}

		// Frame resources
		GFX_Command *cmd = gfx_frame_begin(device);
		if (cmd == 0)
			continue;

		// Swapchain image acquisition
		GFX_Image *compute_blit_target = os_surface_drawable(popup_compute) ? gfx_backbuffer(device, cmd, popup_swapchain) : 0;
		if (compute_blit_target) {
			ASSERT(compute_blit_target->width == compute_image->width && compute_blit_target->height == compute_image->height);
			// transition swapchain target & blit src compute image
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_TRANSFER_DST, compute_blit_target);
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_COMPUTE_SHADER_WRITE, compute_image);

			gfx_cmd_pipeline_bind(cmd, &c_pipeline);
			gfx_bind(device, &c_pipeline, 0, array_arg(Uniform, storage_images(0, (GFX_Image *[]){ compute_image }, 1)));

			// Dispatch compute & Blit to main window surface
			struct {
				float2 mouse;
				float time;
			} pc = {
				.mouse = compute_mouse,
				.time = (float)time,
			};

			vkCmdPushConstants(cmd->handle, c_pipeline.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);
			gfx_cmd_dispatch(cmd, (compute_blit_target->width / 16) + 1, (compute_blit_target->height / 16) + 1, 1);
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_TRANSFER_SRC, compute_image);
			Rectangle area = rect(0, 0, compute_blit_target->width, compute_blit_target->height);
			gfx_cmd_image_blit(cmd, area, compute_image, area, compute_blit_target);
		} else if (os_surface_drawable(popup_compute)) {
			uint2 dims = os_surface_size(popup_compute);
			gfx_swapchain_resize(device, popup_swapchain, dims.x, dims.y);
			gfx_image_resize(device, compute_image, dims.x, dims.y);
		}

		GFX_Image *main_target = gfx_backbuffer(device, cmd, main_swapchain);
		if (main_target) {
			// transition swapchain & offscren targets for drawing
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_COLOR_ATTACHMENT, main_target);
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_COLOR_ATTACHMENT, msaa_target);
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_COLOR_ATTACHMENT, spatial_target);

			static float blink_timer = 0.0f;

			blink_timer += dt;

			// :skinning
			for (uint32_t instance_index = 0; instance_index < scene->entity_count; ++instance_index) {
				Entity *instance = &scene->entities[instance_index];
				if (entity_has(instance, ENTITY_FEATURE_ANIMATE) == false || animation_counts[instance->meshid] == 0 || instance->skin_matrices == 0)
					continue;

				Mesh *mesh = &meshes[instance->meshid];

				uint64_t matrices_size = mesh->skeleton.bone_count * sizeof(float4x4);
				uint64_t matrices_offset = gfx_cmd_put(cmd, matrices_size, instance->skin_matrices);

				uint64_t skinned_vertices_size = alignup(mesh->total_vertex_count * sizeof(Vertex3D), 256);
				instance->skinned_vertices_offset = gfx_cmd_put(cmd, skinned_vertices_size, 0);

				gfx_cmd_pipeline_bind(cmd, &pipeline_skinning);
				struct {
					uint32_t vertex_count;
					uint32_t _pad0;
					uint64_t skinning_matrices_address;
					uint64_t input_address;
					uint64_t skinning_address;
					uint64_t output_address;
				} pc = {
					.vertex_count = mesh->total_vertex_count,
					.skinning_matrices_address = cmd->transient_buffer->address + matrices_offset,
					.input_address = mesh->buffer->address + mesh->buffer_vertex_byte_offset,
					.skinning_address = mesh->buffer->address + mesh->buffer_skinning_data_byte_offset,
					.output_address = cmd->transient_buffer->address + instance->skinned_vertices_offset,
				};
				vkCmdPushConstants(cmd->handle, pipeline_skinning.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);

				gfx_cmd_dispatch(cmd, (mesh->total_vertex_count + 255) / 256, 1, 1);
				gfx_cmd_buffer_barrier(
					cmd,
					RESOURCE_USAGE_COMPUTE_SHADER_WRITE,
					RESOURCE_USAGE_VERTEX_SHADER_READ,
					instance->skinned_vertices_offset,
					skinned_vertices_size, cmd->transient_buffer);
			}

			typedef struct {
				float4x4 view;
				float4x4 proj;
				float4 camera_position;
				float2 viewport;
				float fog_density;
				float ambient_strength;
				float fog_gradient;
				float time;
			} Frame3D;

			float ortho_size = 10.0f;
			lights[light_index].matrix = mul4x4(
				orthographic(-ortho_size, ortho_size, -ortho_size, ortho_size, 0.1f, 100.f),
				lookat(make3_from4(lights[light_index].position), zero3, unit3(UP)));

			Frame3D frame_data = {
				.viewport = { dims.x, dims.y },
				.ambient_strength = ambient_strength,
				.fog_density = fog_density,
				.fog_gradient = fog_gradient,
				.time = time,
			};

			{ // :shadow
				gfx_cmd_image_transition(cmd, RESOURCE_USAGE_DEPTH_ATTACHMENT, shadow_depthbuffer);
				VkRenderingAttachmentInfo depth_attachment = {
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = shadow_depthbuffer->view,
					.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
					.clearValue.depthStencil.depth = 1.0f,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				};

				VkExtent2D extent = {
					.width = shadow_depthbuffer->width,
					.height = shadow_depthbuffer->height,
				};
				VkRenderingInfo shadowpass_info = {
					.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
					.renderArea.extent = extent,
					.layerCount = 1,
					.colorAttachmentCount = 0,
					.pDepthAttachment = &depth_attachment,
				};

				VkDebugUtilsLabelEXT label_info = {
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
					.pLabelName = "SHADOW_PASS",
					.color = { 1.0f, 1.0f, 1.0f, 1.0f },
				};
				vkCmdBeginDebugUtilsLabel(cmd->handle, &label_info);
				vkCmdBeginRendering(cmd->handle, &shadowpass_info);

				VkViewport viewport = {
					.width = extent.width,
					.height = extent.height,
					.minDepth = 0.0f,
					.maxDepth = 1.0f,
				};
				vkCmdSetViewport(cmd->handle, 0, 1, &viewport);
				vkCmdSetScissor(cmd->handle, 0, 1, &(VkRect2D){ .extent = extent });

				frame_data.view = lights[light_index].matrix;
				frame_data.proj = identity4x4();
				frame_data.camera_position = lights[light_index].position;
				frame_data.proj.elements[5] *= -1;

				gfx_cmd_pipeline_bind(cmd, &pipeline_shadow);
				gfx_bind(device, &pipeline_shadow, 0,
					array_arg(Uniform, uniform_data(0, &frame_data, sizeof(frame_data))) //
				);

				// :shadow
				for (uint32_t instance_index = 0; instance_index < scene->entity_count; ++instance_index) {
					Entity *entity = &scene->entities[instance_index];
					if (entity_has(entity, ENTITY_FEATURE_CAST_SHADOW) == false)
						continue;

					Mesh *mesh = &meshes[entity->meshid];

					vkCmdBindIndexBuffer(cmd->handle, geometry->handle, mesh->buffer_index_byte_offset, VK_INDEX_TYPE_UINT32);
					for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
						MeshPart *part = &mesh->parts[part_index];
						Material *material = &mesh->materials[part->material_id];

						float4x4 transform = compose4x4_from_quat(
							entity->transform.translation,
							entity->transform.rotation,
							entity->transform.scale //
						);
						struct {
							float4x4 model;
						} pc = { .model = transform };

						GFX_Buffer *buffer = mesh->buffer;
						uint64_t offset = mesh->buffer_vertex_byte_offset;
						uint64_t size = mesh->total_vertex_count * sizeof(Vertex3D);
						if (entity_has(entity, ENTITY_FEATURE_ANIMATE) && animation_counts[entity->meshid] && entity->skin_matrices) {
							buffer = cmd->transient_buffer;
							offset = entity->skinned_vertices_offset;
						}

						gfx_bind(device, &pipeline_shadow, 1,
							array_arg(Uniform, storage_buffers(0, buffer, offset, size)) //
						);

						vkCmdPushConstants(cmd->handle, pipeline_shadow.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);
						vkCmdDrawIndexed(cmd->handle, part->index_count, 1, part->index_offset, part->vertex_offset, 0);
					}
				}

				vkCmdEndRendering(cmd->handle);
				vkCmdEndDebugUtilsLabel(cmd->handle);
			}

			{ // :spatial
				gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, shadow_depthbuffer);

				VkRenderingAttachmentInfo color_attachments[] = {
					{
					  .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					  .imageView = msaa_target->view,
					  .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					  .resolveImageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
					  .resolveImageView = spatial_target->view,
					  .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
					  .clearValue.color = { { 1.00f, 1.00f, 0.00f, 1.0f } },
					  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					}
				};

				gfx_cmd_image_transition(cmd, RESOURCE_USAGE_DEPTH_ATTACHMENT, depthbuffer);
				VkRenderingAttachmentInfo depth_attachment = {
					.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
					.imageView = depthbuffer->view,
					.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
					.clearValue.depthStencil.depth = 1.0f,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				};

				VkExtent2D extent = {
					.width = main_target->width,
					.height = main_target->height,
				};

				VkRenderingInfo renderpass_info = {
					.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
					.renderArea.extent = extent,
					.layerCount = 1,
					.colorAttachmentCount = countof(color_attachments),
					.pColorAttachments = color_attachments,
					.pDepthAttachment = &depth_attachment,
				};

				VkDebugUtilsLabelEXT label_info = {
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
					.pLabelName = "SPATIAL_PASS",
					.color = { 1.0f, 1.0f, 1.0f, 1.0f },
				};
				vkCmdBeginDebugUtilsLabel(cmd->handle, &label_info);
				vkCmdBeginRendering(cmd->handle, &renderpass_info);

				VkViewport viewport = {
					.width = extent.width,
					.height = extent.height,
					.minDepth = 0.0f,
					.maxDepth = 1.0f,
				};
				vkCmdSetViewport(cmd->handle, 0, 1, &viewport);
				vkCmdSetScissor(cmd->handle, 0, 1, &(VkRect2D){ .extent = extent });

				frame_data.view = lookat(camera->position, camera->target, camera->up);
				frame_data.proj = perspective(deg_to_rad(45.f), (float)dims.x / (float)dims.y, 0.1f, 500.f);
				frame_data.camera_position = make4_from3(camera->position, 0.0f);

				Uniform uniforms[] = {
					uniform_data(0, &frame_data, sizeof(frame_data)),
					storage_data(1, lights + light_index, sizeof(lights[0])),
					sampler_with_textures(2, (GFX_Image *[]){ shadow_depthbuffer }, 1, shadow_sampler),
					sampler_with_textures(3, (GFX_Image *[]){ skybox.handle }, 1, linear_sampler[WRAP_MODE_CLAMP]),
				};
				gfx_bind(device, &pipeline_3d, 0, uniforms, countof(uniforms));

				// :scene
				gfx_cmd_pipeline_bind(cmd, &pipeline_3d);
				for (uint32_t instance_index = 0; instance_index < scene->entity_count; ++instance_index) {
					Entity *entity = &scene->entities[instance_index];
					if (entity_has(entity, ENTITY_FEATURE_DRAW_MESH) == false || entity->pass != DRAW_PASS_OPAQUE)
						continue;

					Mesh *mesh = &meshes[entity->meshid];

					vkCmdBindIndexBuffer(cmd->handle, geometry->handle, mesh->buffer_index_byte_offset, VK_INDEX_TYPE_UINT32);
					for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
						MeshPart *part = &mesh->parts[part_index];
						Material *material = &mesh->materials[part->material_id];

						float4x4 transform = compose4x4_from_quat(
							entity->transform.translation,
							entity->transform.rotation,
							entity->transform.scale //
						);
						struct {
							float4x4 model;
							float4 base_color;
							float4 emissive;
							float2 metallic_roughness;
							float4 uv_transform; // xy = scale, zw = offset
						} pc = {
							.model = transform,
							.base_color = material->tint,
							.emissive = one4,
							.metallic_roughness = { 0.0f, 0.5f },
							.uv_transform = { 1.0f, 1.0f, 0.0f, 0.0f },
						};

						if (entity->meshid == MESH_TERRAIN_FLAT) {
							pc.uv_transform.x *= 8.f;
							pc.uv_transform.y *= 8.f;
						}

						if (entity->meshid == MESH_HERO_MALE && part_index == 3) { // hero head
							if (blink_timer > 2.0f && blink_timer < 2.1f)
								pc.uv_transform.z = 1.0f / 3.0f;
							else if (blink_timer > 2.1f && blink_timer < 2.3f) {
								pc.uv_transform.z = 2.0f / 3.0f;
							} else if (blink_timer >= 2.3f) {
								blink_timer = 0.0f;
							}
						}

						GFX_Buffer *buffer = mesh->buffer;
						uint64_t offset = mesh->buffer_vertex_byte_offset;
						uint64_t size = mesh->total_vertex_count * sizeof(Vertex3D);
						if (entity_has(entity, ENTITY_FEATURE_ANIMATE) && animation_counts[entity->meshid] && entity->skin_matrices) {
							buffer = cmd->transient_buffer;
							offset = entity->skinned_vertices_offset;
						}

						GFX_Image *images[] = {
							[TEXTURE_SLOT_ALBEDO] = material->textures[TEXTURE_SLOT_ALBEDO].handle,
							[TEXTURE_SLOT_METAL_ROUGHNESS] = material->textures[TEXTURE_SLOT_METAL_ROUGHNESS].handle,
							[TEXTURE_SLOT_NORMAL] = material->textures[TEXTURE_SLOT_NORMAL].handle,
							[TEXTURE_SLOT_OCCLUSION] = material->textures[TEXTURE_SLOT_OCCLUSION].handle,
							[TEXTURE_SLOT_EMISSIVE] = material->textures[TEXTURE_SLOT_EMISSIVE].handle,
						};

						Uniform uniforms[] = {
							storage_buffers(0, buffer, offset, size),
							sampler_with_textures(1, images, countof(images), linear_sampler[WRAP_MODE_REPEAT])
						};
						gfx_bind(device, &pipeline_3d, 1, uniforms, countof(uniforms));

						vkCmdPushConstants(cmd->handle, pipeline_3d.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);
						vkCmdDrawIndexed(cmd->handle, part->index_count, 1, part->index_offset, part->vertex_offset, 0);
					}
				}

				// :grass
				if (draw_grass) {
					Mesh *mesh = &meshes[MESH_GRASS_BILLBOARD];

					vkCmdBindIndexBuffer(cmd->handle, geometry->handle, mesh->buffer_index_byte_offset, VK_INDEX_TYPE_UINT32);

					gfx_cmd_pipeline_bind(cmd, &pipeline_grass);
					for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
						MeshPart *part = &mesh->parts[part_index];
						Material *material = &mesh->materials[part->material_id];

						struct {
							float4 base_color;
							float4 emissive;
							float2 metallic_roughness;
							float4 uv_transform;
							uint32_t material_flags;
						} pc = {
							.base_color = material->tint,
							.emissive = one4,
							.metallic_roughness = { 0.0f, 0.5f },
							.uv_transform = { 1.0f, 1.0f, 0.0f, 0.0f },
							.material_flags = use_heightmap << 0,
						};

						GFX_Buffer *buffer = mesh->buffer;
						uint64_t offset = mesh->buffer_vertex_byte_offset;
						uint64_t size = mesh->total_vertex_count * sizeof(Vertex3D);
						GFX_Image *images[] = {
							[TEXTURE_SLOT_ALBEDO] = material->textures[TEXTURE_SLOT_ALBEDO].handle,
							[TEXTURE_SLOT_METAL_ROUGHNESS] = material->textures[TEXTURE_SLOT_METAL_ROUGHNESS].handle,
							[TEXTURE_SLOT_NORMAL] = material->textures[TEXTURE_SLOT_NORMAL].handle,
							[TEXTURE_SLOT_OCCLUSION] = material->textures[TEXTURE_SLOT_OCCLUSION].handle,
							[TEXTURE_SLOT_EMISSIVE] = material->textures[TEXTURE_SLOT_EMISSIVE].handle,
						};

						Uniform uniforms[] = {
							storage_buffers(0, buffer, offset, size),
							sampler_with_textures(1, images, countof(images), nearest_sampler[WRAP_MODE_CLAMP]),
							storage_buffers(2, grass_instancing_buffer, 0, grass_instancing_buffer->size),
							sampler_with_textures(3, (GFX_Image *[]){ noise_image.handle }, 1, linear_sampler[WRAP_MODE_CLAMP]),
						};
						gfx_bind(device, &pipeline_grass, 1, uniforms, countof(uniforms));

						vkCmdPushConstants(cmd->handle, pipeline_grass.layout, VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);
						vkCmdDrawIndexed(cmd->handle, part->index_count, map_width * map_depth, part->index_offset, part->vertex_offset, 0);
					}
				}

				if (draw_skybox) { // :skybox
					gfx_cmd_pipeline_bind(cmd, &pipeline_skybox);
					vkCmdDraw(cmd->handle, 36, 1, 0, 0);
				}

				{ // :transparent

					typedef struct {
						float distance;
						Entity *entity;
					} MeshSort;
					MeshSort *transparent_meshes = arena_push_count(frame_arena, MeshSort, scene->entity_count);

					uint32_t transparent_mesh_count = 0;
					for (uint32_t index = 0; index < scene->entity_count; ++index) {
						Entity *entity = &scene->entities[index];
						if (entity_has(entity, ENTITY_FEATURE_DRAW_MESH) == false || entity->pass != DRAW_PASS_TRANSPARENT)
							continue;

						Mesh *mesh = &meshes[entity->meshid];
						float3 center = add3(entity->transform.translation, aabb3_center(mesh->bounds));

						transparent_meshes[transparent_mesh_count++] = (MeshSort){
							.distance = lensq3(sub3(camera->position, center)),
							.entity = entity
						};
					}
					arena_pop(frame_arena, sizeof(MeshSort) * (scene->entity_count - transparent_mesh_count));

					qsort(transparent_meshes, transparent_mesh_count, sizeof(MeshSort), cmp_mesh_sort);

					if (transparent_mesh_count)
						gfx_cmd_pipeline_bind(cmd, &transparent);

					for (uint32_t index = 0; index < transparent_mesh_count; ++index) {
						Entity *e = transparent_meshes[index].entity;
						Mesh *mesh = &meshes[e->meshid];

						GFX_Buffer *buffer = mesh->buffer;
						uint64_t offset = mesh->buffer_vertex_byte_offset;
						uint64_t size = mesh->total_vertex_count * sizeof(Vertex3D);
						if (entity_has(e, ENTITY_FEATURE_ANIMATE) && animation_counts[e->meshid] && e->skin_matrices) {
							buffer = cmd->transient_buffer;
							offset = e->skinned_vertices_offset;
						}

						GFX_Image *images[] = {
							[TEXTURE_SLOT_ALBEDO] = window_texture.handle,
							[TEXTURE_SLOT_METAL_ROUGHNESS] = white_texture,
							[TEXTURE_SLOT_NORMAL] = white_texture,
							[TEXTURE_SLOT_OCCLUSION] = white_texture,
							[TEXTURE_SLOT_EMISSIVE] = white_texture,
						};

						Uniform uniforms[] = {
							storage_buffers(0, buffer, offset, size),
							sampler_with_textures(1, images, countof(images), linear_sampler[WRAP_MODE_REPEAT])
						};

						gfx_bind(device, &transparent, 1, uniforms, countof(uniforms));

						vkCmdBindIndexBuffer(cmd->handle, geometry->handle, mesh->buffer_index_byte_offset, VK_INDEX_TYPE_UINT32);
						for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
							MeshPart *part = &mesh->parts[part_index];

							struct {
								float4x4 model;
								float4 tint;
								float4 emissive;
								float2 metallic_roughness;
								float4 uv_st;
							} pc = {
								.model = compose4x4_from_quat(e->transform.translation, e->transform.rotation, e->transform.scale),
								.tint = mesh->materials[part->material_id].tint,
								.emissive = mesh->materials[part->material_id].tint,
							};
							float4x4 world_from_object = compose4x4_from_quat(e->transform.translation, e->transform.rotation, e->transform.scale);

							vkCmdPushConstants(cmd->handle, transparent.layout, VK_SHADER_STAGE_ALL, 0, sizeof(world_from_object), world_from_object.elements);
							vkCmdDrawIndexed(cmd->handle, part->index_count, 1, part->index_offset, part->vertex_offset, 0);
						}
					}
				}

				{ // :overlay
					if (batch_line3->offset) {
						gfx_cmd_pipeline_bind(cmd, &pipeline_line3d);
						Uniform uniforms[] = {
							storage_data(0, batch_line3->base, batch_line3->offset),
						};
						gfx_bind(device, &pipeline_line3d, 1, uniforms, countof(uniforms));
						vkCmdDraw(cmd->handle, (batch_line3->offset / sizeof(LineVertex3D)) * 6, 1, 0, 0);
					}

					typedef struct {
						float4x4 view;
						float4x4 projection;
						float2 camera_position;
						float2 viewport;
						float time;
					} Frame2D;

					Frame2D frame_2d = {
						.view = identity4x4(),
						.projection = orthographic(0.0f, dims.x, 0.0f, dims.y, -50.f, 50.f),
						.viewport = cast2(dims, float2),
						.time = time,
					};
					if (batch_line2d->offset) {
						gfx_cmd_pipeline_bind(cmd, &pipeline_line2d);
						Uniform uniforms[] = {
							storage_data(0, batch_line2d->base, batch_line2d->offset),
						};
						gfx_bind(device, &pipeline_line2d, 0, array_arg(Uniform, uniform_data(0, &frame_2d, sizeof(frame_2d))));
						gfx_bind(device, &pipeline_line2d, 1, uniforms, countof(uniforms));
						vkCmdDraw(cmd->handle, (batch_line2d->offset / sizeof(LineVertex3D)) * 6, 1, 0, 0);
					}
				}

				vkCmdEndRendering(cmd->handle);
				vkCmdEndDebugUtilsLabel(cmd->handle);
			}

			if (batch_2d->offset) { // :canvas
				gfx_cmd_image_transition(cmd, RESOURCE_USAGE_COLOR_ATTACHMENT, ui_target);
				VkRenderingAttachmentInfo color_attachments[] = { {
				  .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				  .imageView = ui_target->view,
				  .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				  .clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } },
				  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				} };

				VkExtent2D extent = { ui_target->width, ui_target->height };
				VkRenderingInfo renderpass_info = {
					.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
					.renderArea.extent = extent,
					.layerCount = 1,
					.colorAttachmentCount = countof(color_attachments),
					.pColorAttachments = color_attachments,
					.pDepthAttachment = 0,
				};

				VkDebugUtilsLabelEXT label_info = {
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
					.pLabelName = "UI_PASS",
					.color = { 1.0f, 1.0f, 1.0f, 1.0f },
				};
				vkCmdBeginDebugUtilsLabel(cmd->handle, &label_info);
				vkCmdBeginRendering(cmd->handle, &renderpass_info);

				typedef struct {
					float4x4 view;
					float4x4 projection;
					float2 camera_position;
					float2 viewport;
					float time;
				} Frame2D;

				Frame2D frame_2d = {
					.view = identity4x4(),
					.projection = orthographic(0.0f, dims.x, 0.0f, dims.y, -50.f, 50.f),
					.viewport = cast2(dims, float2),
					.time = time,
				};
				uint32_t vertex_count = batch_2d->offset / sizeof(QuadVertex2D);
				uint32_t quad_count = vertex_count / 6;

				GFX_Image *images[32] = { 0 };
				uint32_t image_count = 1;
				for (uint32_t texture_id = 0; texture_id < 32; ++texture_id)
					images[texture_id] = white_texture;

				for (uint32_t quad_index = 0; quad_index < quad_count; ++quad_index) {
					QuadVertex2D *quad_first_vertex = (QuadVertex2D *)batch_2d->base + (quad_index * 6);

					if (quad_first_vertex->imageid && quad_first_vertex->imageid != indexof(device->image_pool, white_texture)) {
						int32_t found_index = -1;
						for (uint32_t image_index = 1; image_index < image_count; ++image_index) {
							if (indexof(device->image_pool, images[image_index]) == quad_first_vertex->imageid) {
								found_index = image_index;
								break;
							}
						}

						if (found_index == -1) {
							ASSERT(image_count < countof(images) || "Extend sprite batching to support beyond 32 distinct images");
							found_index = image_count++;
							images[found_index] = &device->image_pool[quad_first_vertex->imageid];
						}

						for (uint32_t vertex_index = 0; vertex_index < 6; ++vertex_index) {
							QuadVertex2D *vertex = quad_first_vertex + vertex_index;

							vertex->imageid = found_index;
						}
					}
				}

				gfx_cmd_pipeline_bind(cmd, &pipeline_2d);

				Uniform uniforms0[] = {
					uniform_data(0, &frame_2d, sizeof(frame_2d)),
					storage_data(1, batch_2d->base, batch_2d->offset),
				};
				Uniform uniforms1[] = { sampler_with_textures(0, images, countof(images), nearest_sampler[WRAP_MODE_CLAMP]) };

				gfx_bind(device, &pipeline_2d, 0, uniforms0, countof(uniforms0));
				gfx_bind(device, &pipeline_2d, 1, uniforms1, countof(uniforms1));

				vkCmdDraw(cmd->handle, vertex_count, 1, 0, 0);

				vkCmdEndRendering(cmd->handle);
				vkCmdEndDebugUtilsLabel(cmd->handle);
			}

			{ // :composite
				gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, spatial_target);
				gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, ui_target);

				VkRenderingAttachmentInfo color_attachments[] = { {
				  .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
				  .imageView = main_target->view,
				  .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				  .clearValue.color = { { 1.00f, 1.00f, 0.00f, 1.0f } },
				  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				} };

				VkExtent2D extent = { main_target->width, main_target->height };
				VkRenderingInfo renderpass_info = {
					.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
					.renderArea.extent = extent,
					.layerCount = 1,
					.colorAttachmentCount = countof(color_attachments),
					.pColorAttachments = color_attachments,
					.pDepthAttachment = 0,
				};

				VkDebugUtilsLabelEXT label_info = {
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
					.pLabelName = "BLIT_PASS",
					.color = { 1.0f, 1.0f, 1.0f, 1.0f },
				};
				vkCmdBeginDebugUtilsLabel(cmd->handle, &label_info);
				vkCmdBeginRendering(cmd->handle, &renderpass_info);

				gfx_cmd_pipeline_bind(cmd, &pipeline_composite);

				GFX_Image *images[] = {
					spatial_target,
					ui_target,
				};

				Uniform uniforms[] = {
					sampler_with_textures(0, images, countof(images), linear_sampler[WRAP_MODE_CLAMP]),
				};

				gfx_bind(device, &pipeline_composite, 0, uniforms, countof(uniforms));

				vkCmdDraw(cmd->handle, 6, 1, 0, 0);
				vkCmdEndRendering(cmd->handle);
				vkCmdEndDebugUtilsLabel(cmd->handle);
			}
		}
		gfx_frame_end(device, cmd);

		OS_Timestamp current_ts = os_file_last_modified(s("assets/shaders/compute/bin/test.compute.spv"));
		if (compute_ts != current_ts) {
			LOG_INFO("hot-reloading %s...", c_pipeline.options.debug_name);
			vkDeviceWaitIdle(device->handle); // TODO: Proper synchronization for hot-reload
			gfx_pipeline_destroy(device, &c_pipeline);

			ArenaTemp scratch = arena_scratch_begin(NULL);
			String8 compute_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/compute/bin/test.compute.spv"));
			c_pipeline = compute_pipeline_make(device, compute_bytecode);
			arena_scratch_end(scratch);

			compute_ts = current_ts;
		}

		current_ts = os_file_last_modified(s("assets/shaders/fragment/bin/energyfield.fragment.spv"));
		if (transparent_ts != current_ts) {
			LOG_INFO("hot-reloading %s...", transparent.options.debug_name);
			vkDeviceWaitIdle(device->handle); // TODO: Proper synchronization for hot-reload
			PipelineOptions options = transparent.options;
			memory_copy_array(options.color_attachments, transparent.options.color_attachments);

			gfx_pipeline_destroy(device, &transparent);

			ArenaTemp scratch = arena_scratch_begin(NULL);
			String8 vertex_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/vertex/bin/base.vertex.spv"));
			String8 fragment_bytecode = os_file_read_entire(scratch.arena, s("assets/shaders/fragment/bin/energyfield.fragment.spv"));
			transparent = graphics_pipeline_make(device, vertex_bytecode, fragment_bytecode, options);

			arena_scratch_end(scratch);

			transparent_ts = current_ts;
		}

		device->current_frame_index = (device->current_frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
		arena_reset(frame_arena);
	}

	// :destroy
	vkDeviceWaitIdle(device->handle);
	{
		gfx_pipeline_destroy(device, &c_pipeline);
		gfx_pipeline_destroy(device, &pipeline_skinning);
		gfx_pipeline_destroy(device, &pipeline_shadow);
		gfx_pipeline_destroy(device, &pipeline_3d);
		gfx_pipeline_destroy(device, &pipeline_2d);
		gfx_pipeline_destroy(device, &transparent);
		gfx_pipeline_destroy(device, &pipeline_skybox);
		gfx_pipeline_destroy(device, &pipeline_grass);
		gfx_pipeline_destroy(device, &pipeline_line3d);
		gfx_pipeline_destroy(device, &pipeline_line2d);
		gfx_pipeline_destroy(device, &pipeline_composite);
	}

	gfx_device_destroy(device);

	os_surface_close(main_render);
	os_surface_close(popup_compute);
	os_display_shutdown();
	return 0;
}

Image2D load_image(Arena *arena, String8 path) {
	ArenaTemp scratch = arena_scratch_begin(arena);
	Image2D result = { .format = PIXEL_FORMAT_RGBA8_UNORM };

	bool ok = arena && path.length;

	uint8_t *pixels = 0;
	int32_t channels = 0;
	if (ok) {
		String8 file_content = os_file_read_entire(scratch.arena, path);
		pixels = stbi_load_from_memory(file_content.text, file_content.length, (int32_t *)&result.width, (int32_t *)&result.height, &channels, 4);

		ok = pixels != 0;
		if (ok == false) {
			LOG_WARN("[%s] failed to load", path.text);
			static uint8_t magenta[] = { 255, 0, 255, 255 };
			result.width = result.height = 1;
			result.pixels = magenta;
		}
	}

	if (ok) {
		uint32_t pixel_buffer_size = result.width * result.height * 4;
		result.pixels = arena_push_count(arena, uint8_t, pixel_buffer_size);
		memory_copy(result.pixels, pixels, pixel_buffer_size);
		stbi_image_free(pixels);
	}

	if (ok) {
		String8 filename = str8_filename(path);
		LOG_INFO("'%.*s' loaded sucessfully (%ux%u, %s)", filename.length, filename.text, result.width, result.height, channels == 4 ? "RGBA8" : "RGB8");
	}

	arena_scratch_end(scratch);
	return result;
}

Image2D load_cubemap(Arena *arena, String8 paths[], uint32_t count) {
	Image2D result = { 0 };

	bool ok = arena && paths;

	Image2D images[SIDE_COUNT3];
	if (ok) {
		for (uint32_t face_index = 0; face_index < SIDE_COUNT3; ++face_index) {
			images[face_index] = load_image(arena, paths[face_index]);

			if (face_index > 0)
				ASSERT(images[face_index - 1].width == images[face_index].width && images[face_index - 1].height == images[face_index].height && "all images in cubemap must be equally sized.");
		}
	}

	if (ok) {
		result = images[0];
		result.type = IMAGE_TYPE_CUBE;
	}
	return result;
}

Image2D load_gltf_image(Arena *arena, String8 directory, cgltf_image *image) {
	ArenaTemp scratch = arena_scratch_begin(arena);
	Image2D result = { 0 };

	bool ok = arena && image;

	if (ok) {
		if (image->uri) {
			String8 image_path = str8_filepath_join(scratch.arena, directory, str8_wrap(image->uri));
			result = load_image(arena, image_path);
		} else if (image->buffer_view) {
			const uint8_t *buffer_data = cgltf_buffer_view_data(image->buffer_view);
			uint32_t channels = 0;
			result.pixels = stbi_load_from_memory(buffer_data, image->buffer_view->size, (int32_t *)&result.width, (int32_t *)&result.height, (int32_t *)&channels, 4);
			result.format = PIXEL_FORMAT_RGBA8_SRGB;
		}
	}

	arena_scratch_end(scratch);
	return result;
}

Mesh load_gltf(Arena *arena, String8 path) {
	LOG_INFO("loading [%s]", path.text);

	Mesh result = { 0 };
	cgltf_options options = { 0 };
	cgltf_data *data = 0;

	bool ok = cgltf_parse_file(&options, (char *)path.text, &data) == cgltf_result_success;
	if (ok == false)
		LOG_ERROR("%s - failed to open file", path.text);

	if (ok) {
		ok &= cgltf_load_buffers(&options, data, (char *)path.text) == cgltf_result_success;
		ok &= cgltf_validate(data) == cgltf_result_success;
	}

	String8 directory = str8_directory(path);

	if (ok) { // load materials
		result.material_count = data->materials_count + 1;
		result.materials = arena_push_count(arena, Material, result.material_count);
		result.materials[0] = (Material){
			.tint = one4,
		};

		for (uint32_t material_index = 0; material_index < data->materials_count; ++material_index) {
			cgltf_material *material = &data->materials[material_index];
			Material *out = &result.materials[material_index + 1];
			out->tint = (float4){ 1.0f, 1.0f, 1.0f, 1.0f };

			if (material->has_pbr_metallic_roughness) {
				cgltf_pbr_metallic_roughness *pbr = &material->pbr_metallic_roughness;

				out->tint = wrap4(pbr->base_color_factor);
				out->metallic_roughness = (float2){
					.x = pbr->metallic_factor,
					.y = pbr->roughness_factor,
				};

				if (pbr->base_color_texture.texture) {
					cgltf_image *image = pbr->base_color_texture.texture->image;
					out->textures[TEXTURE_SLOT_ALBEDO] = load_gltf_image(arena, directory, image);
					out->textures[TEXTURE_SLOT_ALBEDO].format = PIXEL_FORMAT_RGBA8_SRGB;
				}

				if (pbr->metallic_roughness_texture.texture) {
					cgltf_image *image = pbr->metallic_roughness_texture.texture->image;
					out->textures[TEXTURE_SLOT_METAL_ROUGHNESS] = load_gltf_image(arena, directory, image);
					out->textures[TEXTURE_SLOT_METAL_ROUGHNESS].format = PIXEL_FORMAT_RGBA8_UNORM;
				}
			}

			if (material->normal_texture.texture) {
				cgltf_image *image = material->normal_texture.texture->image;
				out->textures[TEXTURE_SLOT_NORMAL] = load_gltf_image(arena, directory, image);
				out->textures[TEXTURE_SLOT_NORMAL].format = PIXEL_FORMAT_RGBA8_UNORM;
			}
		}
	}

	if (ok) { // load geometry
		for (uint32_t node_index = 0; node_index < data->nodes_count; ++node_index) {
			cgltf_node *node = &data->nodes[node_index];
			if (node->mesh == 0)
				continue;

			for (uint32_t primitive_index = 0; primitive_index < node->mesh->primitives_count; ++primitive_index) {
				cgltf_primitive *primitive = &node->mesh->primitives[primitive_index];
				if (primitive->type == cgltf_primitive_type_triangles) {
					result.part_count++;
					result.total_vertex_count += primitive->attributes[0].data->count;
					result.total_index_count += primitive->indices->count;
				}
			}
		}

		result.parts = arena_push_count(arena, MeshPart, result.part_count);
		result.vertices = arena_push_count(arena, Vertex3D, result.total_vertex_count);
		result.indices = arena_push_count(arena, uint32_t, result.total_index_count);
		result.skinning = data->skins_count > 0 ? arena_push_count(arena, SkinningVertex3D, result.total_vertex_count) : 0;
		result.bounds = aabb3_empty();

		uint32_t part_offset = 0;
		uint64_t vertex_offset = 0, index_offset = 0;
		for (uint32_t node_index = 0; node_index < data->nodes_count; ++node_index) {
			cgltf_node *node = &data->nodes[node_index];
			if (node->mesh == 0)
				continue;

			float4x4 transform = identity4x4();
			if (node->skin == 0)
				cgltf_node_transform_world(node, transform.elements);
			bool has_transform = equal4x4(identity4x4(), transform) == false;

			for (uint32_t primitive_index = 0; primitive_index < node->mesh->primitives_count; ++primitive_index) {
				cgltf_primitive *primitive = &node->mesh->primitives[primitive_index];
				MeshPart *part = &result.parts[part_offset++];
				part->bounds = aabb3_empty();

				part->vertex_count = primitive->attributes[0].data->count;
				part->vertex_offset = vertex_offset;

				part->index_count = primitive->indices->count;
				part->index_offset = index_offset;
				cgltf_accessor_unpack_indices(primitive->indices, result.indices + index_offset, 4, part->index_count);

				uint8_t skinned = 0;
				for (uint32_t attribute_index = 0; attribute_index < primitive->attributes_count; ++attribute_index) {
					cgltf_attribute *attribute = &primitive->attributes[attribute_index];
					cgltf_accessor *accessor = attribute->data;
					ASSERT(accessor->count == part->vertex_count && "expect all attributes to have same number of vertices");

					uint32_t offset = 0;
					switch (attribute->type) {
						case cgltf_attribute_type_position:
							offset = offsetof(Vertex3D, position);
							if (has_transform == false) {
								part->bounds.min = wrap3(accessor->min);
								part->bounds.max = wrap3(accessor->max);
							}
							break;
						case cgltf_attribute_type_normal:
							offset = offsetof(Vertex3D, normal);
							break;
						case cgltf_attribute_type_tangent:
							offset = offsetof(Vertex3D, tangent);
							break;
						case cgltf_attribute_type_texcoord:
							offset = offsetof(Vertex3D, uv);
							break;
						case cgltf_attribute_type_weights:
							offset = offsetof(SkinningVertex3D, weights);
							skinned++;
							break;
						case cgltf_attribute_type_joints:
							offset = offsetof(SkinningVertex3D, bone_ids);
							skinned++;
							break;
						default:
							continue;
					}

					Vertex3D *mesh_vertices = result.vertices + vertex_offset;
					SkinningVertex3D *mesh_skinning = result.skinning + vertex_offset;

					for (uint32_t vertex_index = 0; vertex_index < part->vertex_count; ++vertex_index) {
						if (attribute->type == cgltf_attribute_type_weights)
							cgltf_accessor_read_float(accessor, vertex_index, (void *)((uint8_t *)(mesh_skinning + vertex_index) + offset), cgltf_num_components(accessor->type));
						else if (attribute->type == cgltf_attribute_type_joints)
							cgltf_accessor_read_uint(accessor, vertex_index, (void *)((uint8_t *)(mesh_skinning + vertex_index) + offset), cgltf_num_components(accessor->type));
						else {
							void *dst = (void *)((uint8_t *)(mesh_vertices + vertex_index) + offset);
							cgltf_accessor_read_float(accessor, vertex_index, dst, cgltf_num_components(accessor->type));
							if (has_transform == false)
								continue;

							if (attribute->type == cgltf_attribute_type_position) {
								float3 *pos = (float3 *)dst;
								float3 new_pos = make3_from4(mul4x4v(transform, make4_from3(*pos, 1.0f)));
								pos->x = new_pos.x;
								pos->y = new_pos.y;
								pos->z = new_pos.z;

								aabb3_expand(&part->bounds, new_pos);
							} else if (attribute->type == cgltf_attribute_type_normal) {
								float3 *norm = (float3 *)dst;
								float3 new_norm = make3_from4(mul4x4v(transform, make4_from3(*norm, 0.0f)));
								*norm = norm3(new_norm);
							} else if (attribute->type == cgltf_attribute_type_tangent) {
								float4 *tan = (float4 *)dst;
								float3 new_tan = make3_from4(mul4x4v(transform, (float4){ tan->x, tan->y, tan->z, 0.0f }));
								float3 norm_tan = norm3(new_tan);
								tan->x = norm_tan.x;
								tan->y = norm_tan.y;
								tan->z = norm_tan.z;
							}
						}
					}
				}

				result.bounds.min = less3(result.bounds.min, part->bounds.min);
				result.bounds.max = more3(result.bounds.max, part->bounds.max);

				if (data->skins_count > 0 && skinned == 0 && node->parent && node->parent->mesh == 0) { // add dummy skinned data
					int32_t parent_bone = -1;
					for (uint32_t joint = 0; joint < data->skins[0].joints_count; joint++) {
						if (data->skins[0].joints[joint] == node->parent) {
							parent_bone = joint;
							break;
						}
					}

					if (parent_bone >= 0) {
						SkinningVertex3D *mesh_skinning = result.skinning + vertex_offset;
						for (uint32_t vertex_index = 0; vertex_index < part->vertex_count; ++vertex_index) {
							mesh_skinning[vertex_index].bone_ids = (uint32x4){ parent_bone, 0, 0, 0 };
							mesh_skinning[vertex_index].weights = (float4){ 1.0f, 0.0f, 0.0f, 0.0f };
						}
					}
				}

				if (primitive->material && primitive->material->has_pbr_metallic_roughness) {
					cgltf_material *material = primitive->material;
					cgltf_pbr_metallic_roughness *pbr = &material->pbr_metallic_roughness;

					if (material)
						part->material_id = cgltf_material_index(data, material) + 1;
				}

				vertex_offset += part->vertex_count;
				index_offset += part->index_count;
			}
		}
	}

	/* if (ok) { // load materials */
	/* 	for (uint32_t material_index = 0; material_index < data->materials_count; ++material_index) { */
	/* 		cgltf_material *material = &data->materials[material_index]; */
	/* 		if (material->has_pbr_metallic_roughness == false) { */
	/* 			LOG_WARN("expect material to use metallic roughness."); */
	/* 			continue; */
	/* 		} */

	/* 		cgltf_pbr_metallic_roughness *pbr = &material->pbr_metallic_roughness; */
	/* 	} */
	/* } */

	if (ok) { // load skeleton
		ASSERT(data->skins_count <= 1 && "expect single skinned mesh per file");
		for (uint32_t skin_index = 0; skin_index < data->skins_count; ++skin_index) {
			cgltf_skin *skin = &data->skins[0];
			cgltf_accessor *accessor = skin->inverse_bind_matrices;
			ASSERT(skin->joints_count == accessor->count && "expect joint count to match matrix count");

			result.skeleton.bone_count = skin->joints_count;
			result.skeleton.bones = arena_push_count(arena, Bone, result.skeleton.bone_count);
			result.skeleton.inverse_rest_matrices = arena_push_count(arena, float4x4, result.skeleton.bone_count);
			result.skeleton.bind_pose_matrices = arena_push_count(arena, float4x4, result.skeleton.bone_count);

			cgltf_accessor_unpack_floats(accessor, (float *)result.skeleton.inverse_rest_matrices, accessor->count * cgltf_num_components(accessor->type));

			for (uint32_t joint_index = 0; joint_index < skin->joints_count; ++joint_index) {
				cgltf_node *joint = skin->joints[joint_index];
				cgltf_node_transform_world(joint, result.skeleton.bind_pose_matrices[joint_index].elements);

				memory_copy(
					result.skeleton.bones[joint_index].name,
					joint->name,
					MIN(str8_wrap(joint->name).length, sizeof_member(Bone, name)));

				bool found = false;
				for (uint32_t search_index = 0; search_index < skin->joints_count; ++search_index)
					if (skin->joints[search_index] == joint->parent) {
						result.skeleton.bones[joint_index].parent = search_index;
						found = true;
					}

				if (found == false)
					result.skeleton.bones[joint_index].parent = -1;
			}
		}
	}

	cgltf_free(data);
	return result;
}

static inline float3 face_orient(float3 v, Side face) {
	float3 result = { 0 };
	switch (face) {
		case SIDE_TOP:
			result = (float3){ v.x, v.y, v.z };
			break;
		case SIDE_BOTTOM:
			result = (float3){ v.x, -v.y, -v.z };
			break;
		case SIDE_RIGHT:
			result = (float3){ v.y, v.z, v.x };
			break;
		case SIDE_LEFT:
			result = (float3){ -v.y, v.z, -v.x };
			break;
		case SIDE_FRONT:
			result = (float3){ v.x, v.z, -v.y };
			break;
		case SIDE_BACK:
			result = (float3){ -v.x, v.z, v.y };
			break;
		default:
			ASSERT(!"invalid orientation passed.");
			break;
	}

	return result;
}

Mesh mesh_sphere(Arena *arena, float3 origin, float radius, uint32_t segments, uint32_t rings) {
	Mesh result = { 0 };
	bool ok = arena;
	if (ok) {
		rings = MAX(rings, 1);
		rings += 2; // end caps
		segments = MAX(segments, 4) + 1; // duplicate seam segment

		result.total_vertex_count = rings * segments;
		result.bounds = aabb3_empty();
		result.vertices = arena_push_count(arena, Vertex3D, result.total_vertex_count);

		uint32_t vertex_cursor = 0;
		for (uint32_t ring = 0; ring < rings; ++ring) {
			float theta = ((float)ring / (rings - 1)) * PIf;
			float ct = cosf(theta), st = sinf(theta);

			for (uint32_t segment = 0; segment < segments; ++segment) {
				float azimuth = ((float)segment / (segments - 1)) * TAU;
				float ca = cosf(azimuth), sa = -sinf(azimuth);

				result.vertices[vertex_cursor++] = (Vertex3D){
					.position = add3(origin, make3(st * ca * radius, ct * radius, st * sa * radius)),
					.normal = { st * ca, ct, st * sa },
					.uv = { (float)segment / (segments - 1), (float)ring / (rings - 1) },
				};

				aabb3_expand(&result.bounds, result.vertices[vertex_cursor - 1].position);
			}
		}

		uint32_t face_count = (segments - 1) * (rings - 1);
		result.total_index_count = face_count * 6;
		result.indices = arena_push_count(arena, uint32_t, result.total_index_count);

		uint32_t index_cursor = 0;
		for (uint32_t face = 0; face < face_count; ++face) {
			uint32_t ring = face / (segments - 1);
			uint32_t segment = face % (segments - 1);
			uint32_t index = ring * segments + segment;

			result.indices[index_cursor++] = index;
			result.indices[index_cursor++] = index + segments;
			result.indices[index_cursor++] = index + segments + 1;

			result.indices[index_cursor++] = index;
			result.indices[index_cursor++] = index + segments + 1;
			result.indices[index_cursor++] = index + 1;
		}

		result.part_count = 1;
		result.parts = arena_push_count(arena, MeshPart, result.part_count);

		result.parts[0].vertex_count = result.total_vertex_count;
		result.parts[0].index_count = result.total_index_count;
		result.parts[0].bounds = result.bounds;

		result.material_count = 1;
		result.materials = arena_push_count(arena, Material, result.material_count);

		result.materials[0] = (Material){
			.tint = { 0.8f, 0.8f, 0.8f, 1.0f },
			.emissive = one4,
			.metallic_roughness = { 0.0f, 0.5f },
		};
	}

	return result;
}

Mesh mesh_cylinder(Arena *arena, float3 origin, float half_height, float bottom_radius, float top_radius, uint32_t segments, uint32_t rings, bool top_cap, bool bottom_cap) {
	Mesh result = { 0 };

	bool ok = arena;
	if (ok) {
		rings += 2; // add top and bottom rings
		segments = MAX(3, segments) + 1;

		// map from 0 to 1
		top_cap = top_cap > 0;
		bottom_cap = bottom_cap > 0;

		uint32_t cap_vertices = 1 + segments;
		result.bounds = aabb3_empty();
		result.total_vertex_count = (segments * rings) + (cap_vertices * (top_cap + bottom_cap));
		result.vertices = arena_push_count(arena, Vertex3D, result.total_vertex_count);

		uint32_t vertex_cursor = 0;
		for (uint32_t ring = 0; ring < rings; ++ring) {
			float rv = (float)ring / (rings - 1);
			float y = half_height - rv * (half_height * 2);
			float r = top_radius + rv * (bottom_radius - top_radius);

			for (uint32_t segment = 0; segment < segments; ++segment) {
				float sv = (float)segment / (segments - 1);

				float a = ((float)segment / (segments - 1)) * TAU;
				float ca = cosf(a), sa = -sinf(a);

				result.vertices[vertex_cursor++] = (Vertex3D){
					.position = add3(origin, (float3){ ca * r, y, sa * r }),
					.normal = { ca, 0.0f, sa },
					.uv = { sv, 1.0 - rv },
				};
				aabb3_expand(&result.bounds, result.vertices[vertex_cursor - 1].position);
			}
		}

		uint32_t center_indices[2] = { 0 };
		uint32_t ring_start_indices[2] = { 0 };

		bool cap_enabled[2] = { top_cap, bottom_cap };

		for (uint32_t end_index = 0; end_index < 2; ++end_index) {
			if (cap_enabled[end_index] == false)
				continue;

			float rv = (float)end_index / 1;
			float y = half_height - rv * (half_height * 2);
			float r = top_radius + rv * (bottom_radius - top_radius);

			uint32_t center_index = center_indices[end_index] = vertex_cursor++;
			result.vertices[center_index] = (Vertex3D){
				.position = add3(origin, (float3){ 0.0f, y, 0.0f }),
				.normal = { 0.0f, (rv - 0.5f) * -2.0f, 0.0f },
				.uv = { 0.5f, 0.5f }
			};
			aabb3_expand(&result.bounds, result.vertices[center_index].position);

			ring_start_indices[end_index] = vertex_cursor;
			for (uint32_t segment = 0; segment < segments; ++segment) {
				float sv = (float)segment / (segments - 1);

				float a = ((float)segment / segments) * TAU;
				float ca = cosf(a), sa = -sinf(a);

				result.vertices[vertex_cursor++] = (Vertex3D){
					.position = add3(origin, (float3){ ca * r, y, sa * r }),
					.normal = { 0.0f, (rv - 0.5f) * -2.0f, 0.0f },
					.uv = { (ca + 1.0f) * 0.5f, (sa + 1.0f) * 0.5f } // Planar mapping
				};
				aabb3_expand(&result.bounds, result.vertices[vertex_cursor - 1].position);
			}
		}

		uint32_t face_count = segments * (rings - 1);

		uint32_t side_index_count = face_count * 6;
		uint32_t cap_index_count = segments * 3 * (top_cap + bottom_cap);

		result.total_index_count = side_index_count + cap_index_count;
		result.indices = arena_push_count(arena, uint32_t, result.total_index_count);

		uint32_t cursor = 0;
		for (uint32_t face = 0; face < face_count; ++face) {
			uint32_t index = face, ring = index / segments;
			uint32_t next_index = (index + 1) / segments > ring ? ring * segments : index + 1;

			result.indices[cursor++] = index;
			result.indices[cursor++] = index + segments;
			result.indices[cursor++] = next_index + segments;

			result.indices[cursor++] = index;
			result.indices[cursor++] = next_index + segments;
			result.indices[cursor++] = next_index;
		}

		for (uint32_t segment = 0; segment < segments; ++segment) {
			uint32_t next = (segment + 1) % segments;

			if (cap_enabled[0]) {
				result.indices[cursor++] = center_indices[0];
				result.indices[cursor++] = ring_start_indices[0] + segment;
				result.indices[cursor++] = ring_start_indices[0] + next;
			}

			if (cap_enabled[1]) {
				result.indices[cursor++] = center_indices[1];
				result.indices[cursor++] = ring_start_indices[1] + next;
				result.indices[cursor++] = ring_start_indices[1] + segment;
			}
		}

		result.part_count = 1;
		result.parts = arena_push_count(arena, MeshPart, result.part_count);

		result.parts[0].vertex_count = result.total_vertex_count;
		result.parts[0].index_count = result.total_index_count;
		result.parts[0].bounds = result.bounds;

		result.material_count = 1;
		result.materials = arena_push_count(arena, Material, result.material_count);

		result.materials[0] = (Material){
			.tint = { 0.8f, 0.8f, 0.8f, 1.0f },
			.emissive = one4,
			.metallic_roughness = { 0.0f, 0.5f },
		};
	}

	return result;
}

// TODO: Fix normal for cones
Mesh mesh_cone(Arena *arena, float3 origin, float height, float radius, uint32_t segments) {
	return mesh_cylinder(arena, origin, height * 0.5f, radius, 0.0f, segments, 0, false, true);
}

Mesh mesh_plane(Arena *arena, Plane p, float width, float height, uint32_t subdivision_x, uint32_t subdivision_z) {
	Mesh result = { 0 };
	result.bounds.min = (float3){ .x = -width * 0.5f, .y = 0.0f, .y = -height * 0.5f };
	result.bounds.max = (float3){ .x = width * 0.5f, .y = 0.0f, .y = height * 0.5f };

	bool ok = arena;

	if (ok) {
		subdivision_x += 2;
		subdivision_z += 2;
		result.total_vertex_count = subdivision_x * subdivision_z;

		result.vertices = arena_push_count(arena, Vertex3D, result.total_vertex_count);

		float3 right = norm3(cross3(p.normal, fabsf(dot3(unit3(UP), p.normal)) >= 0.99f ? unit3(BACKWARD) : unit3(UP)));
		float3 up = cross3(p.normal, right);

		Vertex3D *vertex_cursor = result.vertices;
		for (uint32_t z = 0; z < subdivision_z; ++z) {
			for (uint32_t x = 0; x < subdivision_x; ++x) {
				float3 local = {
					.x = (((float)x / (subdivision_x - 1)) - 0.5f) * width,
					.y = 0.0f,
					.z = (((float)z / (subdivision_z - 1)) - 0.5f) * height,
				};

				*vertex_cursor = (Vertex3D){
					.position = add3(scale3(right, local.x), scale3(up, local.z)),
					.normal = p.normal,
					.uv = { (float)x / (subdivision_x - 1), (float)z / (subdivision_z - 1) },
				};
				vertex_cursor++;
			}
		}

		uint32_t face_count = (subdivision_x - 1) * (subdivision_z - 1);

		result.total_index_count = face_count * 6;
		result.indices = arena_push_count(arena, uint32_t, result.total_index_count);

		uint32_t cursor = 0;
		for (uint32_t face = 0; face < face_count; ++face) {
			uint32_t index = face + face / (subdivision_x - 1);

			result.indices[cursor++] = index;
			result.indices[cursor++] = index + 1;
			result.indices[cursor++] = index + subdivision_x;

			result.indices[cursor++] = index + 1;
			result.indices[cursor++] = index + subdivision_x + 1;
			result.indices[cursor++] = index + subdivision_x;
		}

		result.part_count = 1;
		result.parts = arena_push_count(arena, MeshPart, result.part_count);

		result.parts[0].vertex_count = result.total_vertex_count;
		result.parts[0].index_count = result.total_index_count;

		result.material_count = 1;
		result.materials = arena_push_count(arena, Material, result.material_count);

		result.materials[0] = (Material){
			.tint = { 0.8f, 0.8f, 0.8f, 1.0f },
			.emissive = one4,
			.metallic_roughness = { 0.0f, 0.5f },
		};
	}

	return result;
}

Mesh mesh_heightmap(Arena *arena, Side orientation, float w, float h, Image2D heightmap) {
	Mesh result = { 0 };
	result.bounds.min = (float3){ .x = -w * 0.5f, .y = 0.0f, .y = -h * 0.5f };
	result.bounds.max = (float3){ .x = w * 0.5f, .y = 0.0f, .y = h * 0.5f };

	bool ok = arena;

	if (ok) {
		result.total_vertex_count = heightmap.width * heightmap.height;

		result.vertices = arena_push_count(arena, Vertex3D, result.total_vertex_count);
		for (uint32_t z = 0; z < heightmap.height; ++z) {
			for (uint32_t x = 0; x < heightmap.width; ++x) {
				uint32_t index = x + z * heightmap.width;

				float3 local = {
					.x = (((float)x / (heightmap.width - 1)) - 0.5f) * w,
					.y = ((heightmap.pixels[index * 4] / 255.f) - 0.5f) * 40.f,
					.z = (((float)z / (heightmap.height - 1)) - 0.5f) * h,
				};
				result.vertices[index] = (Vertex3D){
					.position = face_orient(local, orientation),
					.normal = { 0.0f, 1.0f, 0.0f },
					.uv = { (float)x / (heightmap.width - 1), (float)z / (heightmap.height - 1) },
				};
			}
		}

		uint32_t face_count = (heightmap.width - 1) * (heightmap.height - 1);

		result.total_index_count = face_count * 6;
		result.indices = arena_push_count(arena, uint32_t, result.total_index_count);

		uint32_t cursor = 0;
		for (uint32_t face = 0; face < face_count; ++face) {
			uint32_t index = face + face / (heightmap.width - 1);

			result.indices[cursor++] = index;
			result.indices[cursor++] = index + heightmap.width;
			result.indices[cursor++] = index + 1;

			result.indices[cursor++] = index + 1;
			result.indices[cursor++] = index + heightmap.width;
			result.indices[cursor++] = index + heightmap.width + 1;
		}

		result.part_count = 1;
		result.parts = arena_push_count(arena, MeshPart, result.part_count);

		result.parts[0].vertex_count = result.total_vertex_count;
		result.parts[0].index_count = result.total_index_count;

		result.material_count = 1;
		result.materials = arena_push_count(arena, Material, result.material_count);

		result.materials[0] = (Material){
			.tint = { 0.8f, 0.8f, 0.8f, 1.0f },
			.emissive = one4,
			.metallic_roughness = { 0.0f, 0.5f },
		};
	}

	return result;
}

Mesh mesh_merge(Arena *arena, Mesh *meshes, uint32_t mesh_count) {
	Mesh result = { 0 };

	bool ok = arena && meshes && mesh_count;
	if (ok) {
		result.bounds = aabb3_empty();

		for (uint32_t mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
			Mesh *mesh = &meshes[mesh_index];
			ASSERT(mesh->skeleton.bone_count == 0 && "Merging of skeletal meshes not managed");

			result.total_vertex_count += mesh->total_vertex_count;
			result.total_index_count += mesh->total_index_count;
			result.part_count += mesh->part_count;
			result.material_count += mesh->material_count;
		}

		result.vertices = arena_push_count(arena, Vertex3D, result.total_vertex_count);
		result.indices = arena_push_count(arena, uint32_t, result.total_index_count);
		result.parts = arena_push_count(arena, MeshPart, result.part_count);
		result.materials = arena_push_count(arena, Material, result.material_count);

		uint32_t vertex_cursor = 0, index_cursor = 0;
		uint32_t part_cursor = 0, material_cursor = 0;
		for (uint32_t mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
			Mesh *mesh = &meshes[mesh_index];

			memory_copy(result.vertices + vertex_cursor, mesh->vertices, mesh->total_vertex_count * sizeof(Vertex3D));
			memory_copy(result.indices + index_cursor, mesh->indices, mesh->total_index_count * sizeof(uint32_t));
			memory_copy(result.materials + material_cursor, mesh->materials, mesh->material_count * sizeof(Material));

			for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
				MeshPart *part = &mesh->parts[part_index];
				result.parts[part_cursor + part_index] = (MeshPart){
					.vertex_offset = vertex_cursor + part->vertex_offset,
					.vertex_count = part->vertex_count,
					.index_offset = index_cursor + part->index_offset,
					.index_count = part->index_count,
					.bounds = part->bounds,
					.material_id = material_cursor + part->material_id,
				};
			}

			vertex_cursor += mesh->total_vertex_count;
			index_cursor += mesh->total_index_count;
			material_cursor += mesh->material_count;
			part_cursor += mesh->part_count;
			result.bounds = aabb3_merge(result.bounds, mesh->bounds);
		}
	}

	return result;
}

AnimationClip *load_gltf_animations(Arena *arena, String8 path, uint32_t *count) {
	LOG_INFO("loading [%s]", path.text);

	AnimationClip *result = 0;
	cgltf_options options = { 0 };
	cgltf_data *data = 0;

	bool ok = cgltf_parse_file(&options, (char *)path.text, &data) == cgltf_result_success;
	if (ok == false)
		LOG_ERROR("%s - failed to open file", path.text);

	if (ok) {
		ok &= cgltf_load_buffers(&options, data, (char *)path.text) == cgltf_result_success;
		ok &= cgltf_validate(data) == cgltf_result_success;
		ok &= data->animations_count > 0;
	}

	if (ok) { // load animations
		result = arena_push_count(arena, AnimationClip, data->animations_count);
		*count = data->animations_count;

		cgltf_skin *skin = &data->skins[0];
		for (uint32_t anim_index = 0; anim_index < data->animations_count; ++anim_index) {
			cgltf_animation *anim = &data->animations[anim_index];
			AnimationClip *out_anim = result + anim_index;

			uint32_t max_keyframe_count = 0, min_keyframe_count = UINT32_MAX;
			for (uint32_t sampler_index = 0; sampler_index < anim->samplers_count; ++sampler_index) {
				cgltf_animation_sampler *sampler = &anim->samplers[sampler_index];

				max_keyframe_count = MAX(sampler->input->count, max_keyframe_count);
				min_keyframe_count = MIN(sampler->input->count, min_keyframe_count);
				out_anim->keyframe_count = MAX(sampler->input->count, out_anim->keyframe_count);

				float t = 0.0f;
				cgltf_accessor_read_float(sampler->input, sampler->input->count - 1, &t, cgltf_component_size(sampler->input->component_type));

				out_anim->duration = MAX(out_anim->duration, t);
			}
			ASSERT(min_keyframe_count == max_keyframe_count && "expect all sampler timings to match");
			out_anim->keyframes = arena_push_count(arena, Transform3 *, out_anim->keyframe_count);
			out_anim->timings = arena_push_count(arena, float, out_anim->keyframe_count);
			out_anim->bone_count = data->skins[0].joints_count;
			memory_copy(out_anim->name, anim->name, MIN(sizeof(out_anim->name) - 1, str8_wrap(anim->name).length));

			for (uint32_t keyframe = 0; keyframe < out_anim->keyframe_count; ++keyframe) {
				Transform3 *pose = out_anim->keyframes[keyframe];
				out_anim->keyframes[keyframe] = arena_push_count(arena, Transform3, out_anim->bone_count);
			}

			ASSERT(out_anim->bone_count == anim->channels_count / 3 && "expect all channels to be written");
			for (uint32_t channel_index = 0; channel_index < anim->channels_count; ++channel_index) {
				cgltf_animation_channel *channel = &anim->channels[channel_index];
				cgltf_animation_sampler *sampler = channel->sampler;
				ASSERT(sampler->interpolation == cgltf_interpolation_type_linear && "expect all animations to interpolate linearly");

				uint32_t bone_index = -1;
				for (uint32_t index = 0; index < out_anim->bone_count; ++index) {
					if (channel->target_node == skin->joints[index]) {
						bone_index = index;
						break;
					}
				}
				if (bone_index == (uint32_t)-1)
					continue;

				for (uint32_t keyframe = 0; keyframe < sampler->input->count; ++keyframe) {
					cgltf_accessor_read_float(sampler->input, keyframe, &out_anim->timings[keyframe], 1);
					Transform3 *transforms = out_anim->keyframes[keyframe];

					if (channel->target_path == cgltf_animation_path_type_translation)
						cgltf_accessor_read_float(sampler->output, keyframe, (float *)&transforms[bone_index].translation, 3);
					else if (channel->target_path == cgltf_animation_path_type_rotation)
						cgltf_accessor_read_float(sampler->output, keyframe, (float *)&transforms[bone_index].rotation, 4);
					else if (channel->target_path == cgltf_animation_path_type_scale)
						cgltf_accessor_read_float(sampler->output, keyframe, (float *)&transforms[bone_index].scale, 3);
				}
			}
		}
	}
	cgltf_free(data);

	return result;
}

void draw2d_quad(Arena *arena, Rectangle dst, Rectangle src, Image2D *image, float2 origin, float rotation, float border_width, Color border_color, float4 radii, Color fill_color) {
	bool ok = arena;
	if (ok) {
		float2 size = { dst.width, dst.height };
		float2 min = sub2(make2(dst.x, dst.y), origin), max = add2(min, make2(dst.width, dst.height));
		float2 corners[] = { min, { max.x, min.y }, { min.x, max.y }, max };

		float2 uv0 = zero2;
		float2 uv1 = one2;
		if (image) {
			uv0 = make2(src.x / image->width, src.y / image->height);
			uv1 = make2((src.x + src.width) / image->width, (src.y + src.height) / image->height);
		}
		float2 uvs[] = { uv0, { uv1.x, uv0.y }, { uv0.x, uv1.y }, uv1 };

		if (rotation != 0.0f) {
			float2x2 rot = make2x2_from_rotation(DEG2RAD * rotation);
			min = negate2(origin);
			max = add2(min, make2(dst.width, dst.height));

			corners[0] = add2(make2(dst.x, dst.y), mul2x2v(rot, min));
			corners[1] = add2(make2(dst.x, dst.y), mul2x2v(rot, make2(max.x, min.y)));
			corners[2] = add2(make2(dst.x, dst.y), mul2x2v(rot, make2(min.x, max.y)));
			corners[3] = add2(make2(dst.x, dst.y), mul2x2v(rot, max));
		}

		uint32_t imageid = image ? image->handle->imageid : 0;
		// clang-format off
        QuadVertex2D quad[] = {
            // pos      // tex
            (QuadVertex2D){ .position = corners[0], .uv = uvs[0] , .radii = radii, .size = size, .fill_color = color_pack_uint32(fill_color), .border_color = color_pack_uint32(border_color), .imageid = imageid, .border_width = border_width }, 
            (QuadVertex2D){ .position = corners[2], .uv = uvs[2] , .radii = radii, .size = size, .fill_color = color_pack_uint32(fill_color), .border_color = color_pack_uint32(border_color), .imageid = imageid, .border_width = border_width }, 
            (QuadVertex2D){ .position = corners[3], .uv = uvs[3] , .radii = radii, .size = size, .fill_color = color_pack_uint32(fill_color), .border_color = color_pack_uint32(border_color), .imageid = imageid, .border_width = border_width },  

            (QuadVertex2D){ .position = corners[0], .uv = uvs[0] , .radii = radii, .size = size, .fill_color = color_pack_uint32(fill_color), .border_color = color_pack_uint32(border_color), .imageid = imageid, .border_width = border_width }, 
            (QuadVertex2D){ .position = corners[3], .uv = uvs[3] , .radii = radii, .size = size, .fill_color = color_pack_uint32(fill_color), .border_color = color_pack_uint32(border_color), .imageid = imageid, .border_width = border_width }, 
            (QuadVertex2D){ .position = corners[1], .uv = uvs[1] , .radii = radii, .size = size, .fill_color = color_pack_uint32(fill_color), .border_color = color_pack_uint32(border_color), .imageid = imageid, .border_width = border_width }, 
        };
		// clang-format on

		memory_copy(arena_push_count(arena, QuadVertex2D, 6), quad, sizeof(quad));
	}
}

void draw2d_rect_ex(Arena *arena, Rectangle rect, float2 origin, float rotation, Color color) {
	draw2d_quad(arena, rect, rect(0, 0, rect.width, rect.height), 0, origin, rotation, 0.0f, rgba(0, 0, 0, 0), splat4(0.0f), color);
}

void draw2d_rect(Arena *arena, Rectangle rect, Color color) {
	draw2d_rect_ex(arena, rect, zero2, 0.0f, color);
}

void draw2d_rect_outline(Arena *arena, Rectangle rect, float thickness, Color color) {
	draw2d_quad(arena, rect, (Rectangle){ 0 }, 0, zero2, 0, thickness, color, splat4(0.0f), TRANSPARENT);
}

void draw2d_rect_rounded(Arena *arena, Rectangle rect, float4 radii, Color color) {
	draw2d_quad(arena, rect, rect(0, 0, rect.width, rect.height), 0, zero2, 0.0f, 0.0f, rgba(0, 0, 0, 0), radii, color);
}

void draw2d_rect_rounded_ex(Arena *arena, Rectangle rect, float2 origin, float rotation, float4 radii, Color color) {
	draw2d_quad(arena, rect, rect(0.0f, 0.0f, rect.width, rect.height), 0, origin, rotation, 0.0f, rgba(0, 0, 0, 0), radii, color);
}

void draw2_sprite_ex(Arena *arena, Rectangle src, Rectangle dst, Image2D *image, Color tint) {
	draw2d_quad(arena, dst, src, image, zero2, 0.0f, 0.0f, rgba(0, 0, 0, 0), splat4(0.0f), tint);
}

void draw2_sprite(Arena *arena, float2 position, Image2D *image, Color tint) {
	draw2_sprite_ex(arena, rect(0, 0, image->width, image->height), rect(position.x, position.y, image->width, image->height), image, tint);
}

void draw2d_point(Arena *arena, float2 position, float radius, Color color) {
	draw2d_quad(arena, rect(position.x, position.y, radius * 2.0, radius * 2.0), (Rectangle){ 0 }, 0, splat2(radius), 0.0, 0.0, TRANSPARENT, splat4(radius), color);
}

void draw2d_textf(Arena *arena, Font *font, float2 position, Color color, String8 format, ...) {
	ArenaTemp scratch = arena_scratch_begin(arena);

	bool ok = arena && font;
	if (ok) {
		va_list args;
		va_start(args, format);
		String8 text = str8_push_format_list(scratch.arena, format, args);
		va_end(args);

		float y_offset = 0.0f;
		for (uint32_t index = 0; index < text.length; ++index) {
			uint8_t c = text.text[index];
			Glyph *glyph = &font->glyphs[c];
			y_offset = maxf(y_offset, glyph->src.height);
		}

		float x_offset = 0.0f;
		for (uint32_t index = 0; index < text.length; ++index) {
			uint8_t c = text.text[index];
			if (c == '\n') {
				x_offset = 0.0f;
				y_offset += font->bake_size;
			}

			Glyph *glyph = &font->glyphs[c];

			Rectangle dst = {
				.x = position.x + x_offset + (glyph->bearing.x),
				.y = position.y + y_offset + (glyph->bearing.y),
				.width = glyph->src.width,
				.height = glyph->src.height,
			};

			draw2d_quad(arena, dst, glyph->src, &font->atlas, splat2(0.0f), 0.0f, 0.0f, TRANSPARENT, splat4(0.0f), color);

			x_offset += glyph->advance_x;
		}
	}

	arena_scratch_end(scratch);
}

void draw2_triangle(Arena *arena, Triangle2 t, float thickness, Color color) {
	draw2_line(arena, t.a, t.b, thickness, color);
	draw2_line(arena, t.b, t.c, thickness, color);
	draw2_line(arena, t.c, t.a, thickness, color);
}

void draw2_line(Arena *arena, float2 start, float2 end, float thickness, Color color) {
	*arena_push_count(arena, LineVertex3D, 1) = (LineVertex3D){
		.a = make4(start.x, start.y, 0.0f, thickness),
		.b = make4(end.x, end.y, 0.0f, thickness),
		.color = color_pack_uint32(color),
	};
}

void draw2_arrow(Arena *arena, float2 start, float2 end, float thickness, Color color) {
	float2 shaft_end = add2(start, scale2(sub2(end, start), 0.75f));
	draw2_line(arena, start, shaft_end, thickness, color);

	*arena_push_count(arena, LineVertex3D, 1) = (LineVertex3D){
		.a = make4(shaft_end.x, shaft_end.y, 0.0f, thickness * 4.0f),
		.b = make4(end.x, end.y, 0.0f, 0.0f),
		.color = color_pack_uint32(color),
	};
}

void draw3_arc(Arena *arena, float3 center, float radius, uint8_t segments, Side plane, float angle_span, float thickness, Color color) {
	for (uint32_t i = 0; i < segments; ++i) {
		float a = ((float)i / segments) * angle_span;
		float an = ((float)(i + 1) / segments) * angle_span;
		float ca = cosf(a), sa = sinf(a);
		float can = cosf(an), san = sinf(an);

		float3 p0, p1;
		switch (plane) {
			case SIDE_BOTTOM:
			case SIDE_TOP:
				p0 = (float3){ center.x + ca * radius, center.y, center.z + sa * radius };
				p1 = (float3){ center.x + can * radius, center.y, center.z + san * radius };
				break;
			case SIDE_LEFT:
			case SIDE_RIGHT:
				p0 = (float3){ center.x, center.y + sa * radius, center.z + ca * radius };
				p1 = (float3){ center.x, center.y + san * radius, center.z + can * radius };
				break;
			case SIDE_BACK:
			case SIDE_FRONT:
				p0 = (float3){ center.x + ca * radius, center.y + sa * radius, center.z };
				p1 = (float3){ center.x + can * radius, center.y + san * radius, center.z };
				break;

			default:
				ASSERT(false);
				break;
		}

		draw3_line(arena, p0, p1, thickness, color);
	}
}

void draw3_sphere_outline(Arena *arena, float3 center, float radius, uint8_t segments, float thickness, Color color) {
	draw3_arc(arena, center, radius, segments, SIDE_TOP, TAU, thickness, color);
	draw3_arc(arena, center, radius, segments, SIDE_RIGHT, TAU, thickness, color);
	draw3_arc(arena, center, radius, segments, SIDE_FRONT, TAU, thickness, color);
}

void draw3_capsule_outline(Arena *arena, float3 a, float3 b, float radius, uint8_t segments, float thickness, Color color) {
	LineVertex3D *spine_points = arena_push_count(arena, LineVertex3D, 8);
	LineVertex3D spine[] = {
		{ { a.x - radius, a.y, a.z, thickness }, { a.x - radius, b.y, a.z, thickness }, color_pack_uint32(WHITE), zero3 },
		{ { a.x + radius, a.y, a.z, thickness }, { a.x + radius, b.y, a.z, thickness }, color_pack_uint32(WHITE), zero3 },
		{ { a.x, a.y, a.z - radius, thickness }, { a.x, b.y, a.z - radius, thickness }, color_pack_uint32(WHITE), zero3 },
		{ { a.x, a.y, a.z + radius, thickness }, { a.x, b.y, a.z + radius, thickness }, color_pack_uint32(WHITE), zero3 },
	};
	memory_copy_array(spine_points, spine);

	for (uint32_t end = 0; end < 2; ++end) {
		float3 c = end == 0 ? a : b;
		float signed_r = (end == 0) ? -radius : radius;

		draw3_arc(arena, c, radius, segments, SIDE_TOP, TAU, thickness, WHITE);
		draw3_arc(arena, c, signed_r, segments, SIDE_RIGHT, PIf, thickness, WHITE);
		draw3_arc(arena, c, signed_r, segments, SIDE_FRONT, PIf, thickness, WHITE);
	}
}

void draw3_aabb_outline(Arena *arena, AABB3 aabb3, float thickness, Color color) {
	float3 min = aabb3.min;
	float3 max = aabb3.max;
	float3 bounding_box_size = sub3(max, min);

	LineVertex3D outline[] = {
		{ { min.x, min.y, min.z, thickness }, { min.x, max.y, min.z, thickness }, color_pack_uint32(color), zero3 },
		{ { min.x, min.y, max.z, thickness }, { min.x, max.y, max.z, thickness }, color_pack_uint32(color), zero3 },
		{ { max.x, min.y, min.z, thickness }, { max.x, max.y, min.z, thickness }, color_pack_uint32(color), zero3 },
		{ { max.x, min.y, max.z, thickness }, { max.x, max.y, max.z, thickness }, color_pack_uint32(color), zero3 },
		{ { min.x, min.y, min.z, thickness }, { min.x, min.y, max.z, thickness }, color_pack_uint32(color), zero3 },
		{ { min.x, min.y, min.z, thickness }, { max.x, min.y, min.z, thickness }, color_pack_uint32(color), zero3 },
		{ { max.x, min.y, max.z, thickness }, { max.x, min.y, min.z, thickness }, color_pack_uint32(color), zero3 },
		{ { max.x, min.y, max.z, thickness }, { min.x, min.y, max.z, thickness }, color_pack_uint32(color), zero3 },
		{ { min.x, max.y, min.z, thickness }, { min.x, max.y, max.z, thickness }, color_pack_uint32(color), zero3 },
		{ { min.x, max.y, min.z, thickness }, { max.x, max.y, min.z, thickness }, color_pack_uint32(color), zero3 },
		{ { max.x, max.y, max.z, thickness }, { max.x, max.y, min.z, thickness }, color_pack_uint32(color), zero3 },
		{ { max.x, max.y, max.z, thickness }, { min.x, max.y, max.z, thickness }, color_pack_uint32(color), zero3 },
	};

	LineVertex3D *points = arena_push_count(arena, LineVertex3D, countof(outline));
	memory_copy_array(points, outline);
}

void draw3_line(Arena *arena, float3 start, float3 end, float thickness, Color color) {
	*arena_push_count(arena, LineVertex3D, 1) = (LineVertex3D){
		.a = make4_from3(start, thickness),
		.b = make4_from3(end, thickness),
		.color = color_pack_uint32(color),
	};
}

void draw3_arrow(Arena *arena, float3 start, float3 end, float thickness, Color color,
	float4x4 view, float4x4 projeciton, float viewport_width) {
	float3 direction = sub3(end, start);
	float total_world_length = len3(direction);
	if (total_world_length < EPSILON)
		return;

	float3 dir_norm = scale3(direction, 1.0f / total_world_length);

	float4x4 vp = mul4x4(projeciton, view);
	float w = vp.elements[3] * end.x + vp.elements[7] * end.y + vp.elements[11] * end.z + vp.elements[15] * 1.0f;
	float desired_pixel_length = thickness * 5.0f;

	float world_head_length = (2.0f * w * desired_pixel_length) / (projeciton.elements[0] * viewport_width);
	if (world_head_length > total_world_length * 0.5f)
		world_head_length = total_world_length * 0.5f;

	float3 shaft_end = sub3(end, scale3(dir_norm, world_head_length));

	draw3_line(arena, start, shaft_end, thickness, color);
	*arena_push_count(arena, LineVertex3D, 1) = (LineVertex3D){
		.a = make4_from3(shaft_end, thickness * 4.0f),
		.b = make4_from3(end, 0.0f),
		.color = color_pack_uint32(color),
	};
}

void draw3_triangle_outline(Arena *arena, Triangle3 t, float thickness, Color color) {
	draw3_line(arena, t.a, t.b, thickness, color);
	draw3_line(arena, t.b, t.c, thickness, color);
	draw3_line(arena, t.c, t.a, thickness, color);
}

void draw3_quad_outline(Arena *arena, Plane plane, float width, float height, float thickness, Color color) {
	float3 right = { 0 }, up = { 0 };
	float dot = dot3(plane.normal, unit3(UP));
	if (fabsf(dot) >= 0.99f) {
		right.x = dot > 0 ? 1.0f : -1.0f;
		up.z = -1.0f;
	} else {
		right = norm3(cross3(plane.normal, (float3){ 0.0f, 1.0f, 0.0f }));
		up = norm3(cross3(plane.normal, right));
	}

	float3 center = scale3(plane.normal, plane.distance);
	float3 h = scale3(right, width * 0.5f);
	float3 v = scale3(up, height * 0.5f);

	float3 corners[] = {
		add3(sub3(center, h), v),
		add3(add3(center, h), v),
		sub3(add3(center, h), v),
		sub3(sub3(center, h), v),
	};

	for (uint32_t index = 0; index < countof(corners); ++index)
		draw3_line(arena, corners[index], corners[(index + 1) % countof(corners)], thickness, color);
}

void draw3_shape_outline(Arena *arena, Shape3 *shape, float3 offset, float thickness) {
	switch (shape->kind) {
		case SHAPE_KIND_AABB3:
			draw3_aabb_outline(arena, aabb3_move(shape->as.aabb3, offset), thickness, WHITE);
			break;
		case SHAPE_KIND_SPHERE: {
			float3 c = add3(shape->as.sphere.center, offset);
			float r = shape->as.sphere.radius;
			uint8_t segments = 32;

			draw3_sphere_outline(arena, c, r, segments, thickness, WHITE);
		} break;
		case SHAPE_KIND_CAPSULE3: {
			float r = shape->as.capsule.radius;
			float3 centers[] = {
				add3(shape->as.capsule.a, offset),
				add3(shape->as.capsule.b, offset),
			};
			draw3_capsule_outline(arena, centers[0], centers[1], r, 32, thickness, WHITE);
		} break;
			break;
		case SHAPE_KIND_PLANE:
			break;
		case SHAPE_KIND_CONVEX_POLYGON:
			break;
		case SHAPE_KIND_MAX:
			break;
	}
}
