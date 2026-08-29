#include "app/scene.h"

#include "meta.h"
#include "res/tables.h"
#include "draw.h"

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
#include "utils/input.h"

#include "os.h"

#include "gfx.h"
#include "gfx/gfx_types.h"
#include "gfx/vulkan/tables.h"

#include <draw.h>
#include <draw/font.h>
#include <draw/imgui.h>
#include <draw/camera.h>

#include <math.h>

#include <cgltf/cgltf.h>
#include <stb/stb_image.h>
#include <vulkan/vulkan_core.h>

typedef enum {
	ICON_PLAY,
	ICON_PAUSE,
	ICON_STOP,

	ICON_MAX,
} IconID;

String8 iconid_to_string[ICON_MAX] = {
	ENUM_STRING_TABLE_ENTRY(ICON, PLAY),
	ENUM_STRING_TABLE_ENTRY(ICON, PAUSE),
	ENUM_STRING_TABLE_ENTRY(ICON, STOP),
};
String8 iconid_to_filepath[ICON_MAX] = {
	[ICON_PLAY] = scomp("assets/icons/PNG/White/1x/forward.png"),
	[ICON_PAUSE] = scomp("assets/icons/PNG/White/1x/pause.png"),
	[ICON_STOP] = scomp("assets/icons/PNG/White/1x/stop.png")
};

typedef enum {
	TEXTURE_SLOT_ALBEDO,
	TEXTURE_SLOT_METAL_ROUGHNESS,
	TEXTURE_SLOT_NORMAL,
	TEXTURE_SLOT_OCCLUSION,
	TEXTURE_SLOT_EMISSIVE,

	TEXTURE_SLOT_COUNT,
} TextureSlot;

String8 texture_slot_to_string[TEXTURE_SLOT_COUNT] = {
	[TEXTURE_SLOT_ALBEDO] = scomp("albedo"),
	[TEXTURE_SLOT_METAL_ROUGHNESS] = scomp("metal_roughness"),
	[TEXTURE_SLOT_NORMAL] = scomp("normal"),
	[TEXTURE_SLOT_OCCLUSION] = scomp("occlusion"),
	[TEXTURE_SLOT_EMISSIVE] = scomp("emissive"),
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

static const String8 meshid_to_display_string[MESH_MAX] = {
	[MESH_HERO_MALE] = scomp("hero male"),
	[MESH_GDBOT] = scomp("gdbot"),
	[MESH_MAGE] = scomp("mage"),
	[MESH_BARREL] = scomp("barrel"),
	[MESH_ROOM] = scomp("room"),
	[MESH_TEST_LEVEL] = scomp("test level"),
	[MESH_ROOM_LARGE] = scomp("room large"),
	[MESH_TERRAIN_FLAT] = scomp("terrain flat"),
	[MESH_TERRAIN_HEIGHTMAP] = scomp("terrain heightmap"),
	[MESH_GRASS_BILLBOARD] = scomp("grass billboard"),
	[MESH_CYLINDER] = scomp("cylinder"),
	[MESH_SPHERE] = scomp("sphere"),
	[MESH_GIZMOS_ARROW] = scomp("arrow"),
};

String8 meshid_to_metadata[MESH_MAX] = {
	[MESH_HERO_MALE] = scomp("assets/models/hero_male.glb"),
	[MESH_GDBOT] = scomp("assets/models/gdbot.glb"),
	[MESH_MAGE] = scomp("assets/models/mage.glb"),
	[MESH_BARREL] = scomp("assets/models/barrel.glb"),
	[MESH_ROOM] = scomp("assets/models/room.glb"),
	[MESH_ROOM_LARGE] = scomp("assets/models/room-large.glb"),
	[MESH_TEST_LEVEL] = scomp("assets/models/test_level.glb"),
	[MESH_GRASS_BILLBOARD] = scomp("assets/models/grass.glb"),
};

typedef enum {
	FONT_PIXELOID_SANS,
	FONT_IBM_PLEX_MONO,

	FONT_MAX,
} FontID;

String8 font_to_string[FONT_MAX] = {
	ENUM_STRING_TABLE_ENTRY(FONT, PIXELOID_SANS),
	ENUM_STRING_TABLE_ENTRY(FONT, IBM_PLEX_MONO),
};

String8 font_to_filepath[FONT_MAX] = {
	[FONT_PIXELOID_SANS] = scomp("assets/fonts/PixeloidSans.ttf"),
	[FONT_IBM_PLEX_MONO] = scomp("/usr/share/fonts/TTF/IBMPlexMono-Regular.ttf"),
};

typedef enum {
	ENTITY_FEATURE_DRAW_MESH,
	ENTITY_FEATURE_CAST_SHADOW,
	ENTITY_FEATURE_TRANSPARENT,

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
	ENUM_STRING_TABLE_ENTRY(ENTITY_FEATURE, TRANSPARENT),
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
		result->transform.scale = splat3(1.0f);
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
				while ((t = lexer_advance(&lexer)).type != TOKEN_EOF) {
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
		  .usage = IMAGE_USAGE_RENDER | IMAGE_USAGE_SAMPLE | IMAGE_USAGE_TRANSFER,
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

	Font fonts[FONT_MAX][FONT_BAKE_SIZE_MAX] = { 0 };
	{ // :fonts
		ArenaTemp scratch = arena_scratch_begin(0);
		uint32_t font_cursor = 0;

		for (FontID id = 0; id < FONT_MAX; ++id) {
			Font *font = fonts[id];

			for (FONT_BakeSize bake_size_index = 0; bake_size_index < FONT_BAKE_SIZE_MAX; ++bake_size_index) {
				Font *font_size = &font[bake_size_index];
				*font_size = load_font(scratch.arena, font_to_filepath[id], font_bake_size_to_value[bake_size_index]);
				Glyph *glyphs = font_size->glyphs;
				font_size->glyphs = arena_push_count(permanent, Glyph, font_size->glyph_count);

				memory_copy(font_size->glyphs, glyphs, sizeof(Glyph) * font_size->glyph_count);

				Image2D *atlas = &font_size->atlas;
				atlas->handle = gfx_image_make(device, atlas->width, atlas->height,
					(ImageOptions){
					  .debug_name = (char *)str8_pushf(scratch.arena, s("%.*s:%d"), sspread(font_to_string[id]), font_bake_size_to_value[bake_size_index]).text,
					  .format = PIXEL_FORMAT_RGBA8_UNORM,
					  .pixels = atlas->pixels,
					});
			}
		}

		arena_scratch_end(scratch);
	}

	Image2D icons[ICON_MAX] = { 0 };
	{ // :icons
		ArenaTemp scratch = arena_scratch_begin(0);
		for (uint32_t index = 0; index < ICON_MAX; ++index) {
			icons[index] = load_image(scratch.arena, iconid_to_filepath[index]);

			Image2D *icon = &icons[index];
			icon->handle = gfx_image_make(device, icon->width, icon->height,
				(ImageOptions){
				  .debug_name = (char *)iconid_to_string[index].text,
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

	Image2D heart_image = load_image(permanent, s("assets/textures/heart.png"));
	heart_image.handle = gfx_image_make(device, heart_image.width, heart_image.height,
		(ImageOptions){
		  .debug_name = "heart",
		  .format = PIXEL_FORMAT_RGBA8_SRGB,
		  .pixels = heart_image.pixels,
		});

	GFX_Sampler *linear_sampler[WRAP_MODE_MAX] = {
		[WRAP_MODE_REPEAT] = gfx_sampler_make(device, sampler_opt("default:linear_repeat", FILTER_LINEAR, WRAP_MODE_REPEAT)),
		[WRAP_MODE_CLAMP] = gfx_sampler_make(device, sampler_opt("default:linear_clamp", FILTER_LINEAR, WRAP_MODE_CLAMP))
	};
	GFX_Sampler *nearest_sampler[WRAP_MODE_MAX] = {
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

	OS_Timestamp shader_ts[SHADER_MAX] = { 0 };
	GFX_Shader *shaders[SHADER_MAX] = { 0 };
	for (ShaderID ID = 0; ID < SHADER_MAX; ++ID) { // :shaders
		ShaderMetadata *metadata = &shaderid_to_metadata[ID];
		if (metadata->filepaths[SHADER_STAGE_VERTEX].length == 0 &&
			metadata->filepaths[SHADER_STAGE_FRAGMENT].length == 0 &&
			metadata->filepaths[SHADER_STAGE_COMPUTE].length == 0)
			continue;

		ArenaTemp scratch = arena_scratch_begin(NULL);
		bool is_compute = metadata->filepaths[SHADER_STAGE_COMPUTE].length > 0;
		if (is_compute) {
			shader_ts[ID] = os_file_last_modified(metadata->filepaths[SHADER_STAGE_COMPUTE]);
			String8 bytecode = os_file_read_entire(scratch.arena, metadata->filepaths[SHADER_STAGE_COMPUTE]);
			shaders[ID] = gfx_compute_make(device, bytecode, (char *)metadata->name.text);
		} else {
			String8 vs_bytecode = os_file_read_entire(scratch.arena, metadata->filepaths[SHADER_STAGE_VERTEX]);
			String8 fs_bytecode = os_file_read_entire(scratch.arena, metadata->filepaths[SHADER_STAGE_FRAGMENT]);

			OS_Timestamp fs_ts = os_file_last_modified(metadata->filepaths[SHADER_STAGE_FRAGMENT]);
			OS_Timestamp vs_ts = os_file_last_modified(metadata->filepaths[SHADER_STAGE_VERTEX]);

			shader_ts[ID] = MAX(fs_ts, vs_ts);
			shaders[ID] = gfx_shader_make(device, vs_bytecode, fs_bytecode, (char *)metadata->name.text);
			for (uint32_t permutation = 0; permutation < metadata->pipeline_count; ++permutation)
				gfx_pipeline_ensure(device, shaders[ID], metadata->pipelines[permutation]);
		}

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

		float mul = 5.0f;
		float shaft_half_height = 0.075f * mul;
		float shaft_radius = 0.005f * mul;
		float head_height = 0.050f * mul;
		float head_radius = 0.018f * mul;

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
				*arena_push_count(cmd->transient_arena, float4x4, 1) = mul4x4(make4x4_rotation(unit3(UP), randf_range(0, TAU)), make4x4_translation(pos));
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

	Camera cameras[VIEWPORT_STATE_COUNT] = {
		[VIEWPORT_STATE_EDITOR] = {
		  .projection = CAMERA_PROJECTION_PERSPECTIVE,
		  .position = { 0.0f, 1.5f, 20.f },
		  .target = { 0.0f, 1.5f, 0.0f },
		  .up = unit3(UP),
		  .fovy = 45.f,
		  .near = 0.1f,
		  .far = 500.0f,
		},
		[VIEWPORT_STATE_GAME] = {
		  .projection = CAMERA_PROJECTION_PERSPECTIVE,
		  .position = { 0.0f, 1.5f, 20.f },
		  .target = { 0.0f, 1.5f, 0.0f },
		  .up = unit3(UP),
		  .fovy = 45.f,
		  .near = 0.1f,
		  .far = 500.0f,
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

	for (bool is_open = true; is_open;) {
		double time = os_time_ns() * 1e-9 - start_time * 1e-9;
		dt = time - last_frame;
		last_frame = time;

		input_update();
		OS_Event event = { 0 };
		for (OS_Event event; os_event_poll(&event);) {
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
		Arena batch_quad2d[] = { {
		  .base = arena_push_count(frame_arena, DRAW_Quad2D, 6 * 1024),
		  .capacity = sizeof(DRAW_Quad2D) * 6 * 1024,
		} };
		Arena batch_line3d[] = { {
		  .base = arena_push_count(frame_arena, DRAW_Line3D, 6 * 2048),
		  .capacity = sizeof(DRAW_Line3D) * 6 * 2048,
		} };

		uint2 dims = os_surface_size(main_render);
		float2 mouse_delta = cast2(input_mouse_delta(), float2);
		float2 mouse = cast2(input_mouse_position(), float2);
		Rectangle viewport = { 0.0f, 0.0f, dims.x, dims.y };
		mouse_delta.x /= dims.x;
		mouse_delta.y /= dims.y;

		if (input_key_pressed(KEY_CODE_TAB))
			state = (state + 1) % VIEWPORT_STATE_COUNT;

		Camera *camera = &cameras[state];

		float4x4 view = camera_view(camera);
		float4x4 proj = camera_proj(camera, viewport.width / viewport.height);
		float4x4 view_proj = mul4x4(proj, view);

		imgui_frame_begin(&imgui,
			(IMGUI_Mouse){
			  .last_position = imgui.mouse.last_position,
			  .position = mouse,
			  .pressed[MOUSE_BUTTON_LEFT] = input_mouse_pressed(MOUSE_BUTTON_LEFT),
			  .released[MOUSE_BUTTON_LEFT] = input_mouse_released(MOUSE_BUTTON_LEFT),
			},
			dt);
		imgui.default_font = fonts[FONT_IBM_PLEX_MONO] + FONT_BAKE_SIZE_16;

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

				{ // :editor
					static float2 mouse_grab_offset = { 0 };
					static IMGUI_Dock panel_dock = IMGUI_DOCK_NONE;

					IMGUI_Widget *panel = imgui_widget_opt(__LINE__,
						(IMGUI_Style){
						  .flow = IMGUI_VERTICAL,
						  .sizing = {
							panel_dock == IMGUI_DOCK_CENTER || panel_dock == IMGUI_DOCK_TOP || panel_dock == IMGUI_DOCK_BOTTOM ? IMGUI_SIZING_GROW : IMGUI_SIZING_FIT,
							panel_dock == IMGUI_DOCK_CENTER || panel_dock == IMGUI_DOCK_LEFT || panel_dock == IMGUI_DOCK_RIGHT ? IMGUI_SIZING_GROW : IMGUI_SIZING_FIT,
						  },
						  .border_radius = splat4(0.0f),
						  .bg = RED,
						});

					Rectangle c = imgui_rect_cached(panel);
					float2 panel_offset = panel_dock == 0 ? (float2){ c.x, c.y } : (float2){ 0 };
					panel->offset[0] = panel_offset.x, panel->offset[1] = panel_offset.y;
					panel->size[0] = 350.f, panel->size[1] = 500.f;

					IMGUI_Widget *root = imgui_widget_opt(__LINE__,
						(IMGUI_Style){
						  .sizing = { IMGUI_SIZING_FIXED, IMGUI_SIZING_FIXED },
						});
					root->size[0] = viewport.width, root->size[1] = viewport.height;

					if (panel_dock) {
						if (panel_dock == IMGUI_DOCK_CENTER)
							imgui_parent(panel, root);
						else {
							IMGUI_Widget *container = imgui_widget_opt(__LINE__,
								(IMGUI_Style){
								  .flow = panel_dock == IMGUI_DOCK_TOP || panel_dock == IMGUI_DOCK_BOTTOM ? IMGUI_VERTICAL : IMGUI_HORIZONTAL,
								  .sizing = { IMGUI_SIZING_GROW, IMGUI_SIZING_GROW },
								});
							imgui_parent(container, root);

							uint32_t dock_index = panel_dock < IMGUI_DOCK_CENTER ? 0 : 2;
							for (uint32_t index = 0; index < 3; ++index) {
								IMGUI_Widget *division = imgui_widget_opt(0,
									(IMGUI_Style){
									  .sizing = { IMGUI_SIZING_GROW, IMGUI_SIZING_GROW },
									  .align = { index, index },
									});
								imgui_parent(division, container);

								if (dock_index == index)
									imgui_parent(panel, division);
							}
						}
					}

					imgui_push_parent(root);
					IMGUI_Widget *topbar = imgui_widget_opt(__LINE__,
						(IMGUI_Style){
						  .sizing = { IMGUI_SIZING_GROW, IMGUI_SIZING_FIT },
						  .p = 4.0f,
						  .gap = 1.0f,
						  .bg = hex(0x151b23),
						  .border_radius = splat4(0.0f),
						});

					imgui_push_parent(topbar);
					{ // topbar
						IMGUI_Style topbar_btn = {
							.sizing[1] = IMGUI_SIZING_GROW,
							.align = { 0, IMGUI_ALIGN_CENTER },
							.ph = 8.0f,
							.pv = 0.0f,
							.bg = hex(0x262c36),
							.fg = WHITE,
							.border_radius = splat4(4.0f),
						};

						imgui_push_style(topbar_btn);

						imgui_spacer();
						if (imgui_button_image(&icons[ICON_PLAY], 0.5f).released) { state = (state + 1) % VIEWPORT_STATE_COUNT; }
						if (imgui_button_image(&icons[ICON_PAUSE], 0.5f).released) { LOG_INFO("Pause"); }
						if (imgui_button_image(&icons[ICON_STOP], 0.5f).released) { LOG_INFO("Stop"); }
						imgui_pop_style(1);

						imgui_pop_parent();
					}

					IMGUI_Widget *body = imgui_widget_opt(__LINE__,
						(IMGUI_Style){
						  .flow = IMGUI_VERTICAL,
						  .sizing = { IMGUI_SIZING_GROW, IMGUI_SIZING_GROW },
						  .p = 12.0f,
						  .gap = 12.0f,
						  .bg = hex(0x0d1117),
						  .border_radius = splat4(0.0f),
						});

					static bool dropdown_active = false;
					IMGUI_Widget *drop_down = 0;
					String8 light_setting_name[] = {
						s("Day"),
						s("Dawn"),
						s("Night"),
					};

					imgui_push_parent(body);
					{
						imgui_push_parent(imgui_widget_opt(__LINE__,
							(IMGUI_Style){
							  .flow = IMGUI_VERTICAL,
							  .gap = 8.0f,
							  .sizing[0] = IMGUI_SIZING_GROW,
							}));
						{
							IMGUI_Style slider = {
								.bg = hex(0x262c36),
								.fg = WHITE,
								.gap = 0.0f,
								.font = fonts[FONT_IBM_PLEX_MONO] + FONT_BAKE_SIZE_12,
								.border_radius = splat4(4.0f)
							};
							imgui_push_style(slider);

							imgui_push_parent(imgui_widget_opt(__LINE__, (IMGUI_Style){ .flow = IMGUI_HORIZONTAL, .sizing[0] = IMGUI_SIZING_GROW }));
							{
								imgui_push_parent(imgui_widget_opt(__LINE__, (IMGUI_Style){ .sizing[0] = IMGUI_SIZING_GROW }));
								imgui_label(__LINE__, s("Fog Density"));
								imgui_pop_parent();

								imgui_push_parent(imgui_widget_opt(__LINE__, (IMGUI_Style){ .sizing[0] = IMGUI_SIZING_GROW }));
								imgui_sliderf(__LINE__, &fog_density, 0.001f, 0.05f);
								imgui_pop_parent();

								imgui_pop_parent();
							}

							imgui_push_parent(imgui_widget_opt(__LINE__, (IMGUI_Style){ .flow = IMGUI_HORIZONTAL, .sizing[0] = IMGUI_SIZING_GROW }));
							{
								imgui_push_parent(imgui_widget_opt(__LINE__, (IMGUI_Style){ .sizing[0] = IMGUI_SIZING_GROW }));
								imgui_label(__LINE__, s("Fog Gradient"));
								imgui_pop_parent();

								imgui_push_parent(imgui_widget_opt(__LINE__, (IMGUI_Style){ .sizing[0] = IMGUI_SIZING_GROW }));
								imgui_sliderf(__LINE__, &fog_gradient, 0.0f, 15.0f);
								imgui_pop_parent();

								imgui_pop_parent();
							}

							imgui_push_parent(imgui_widget_opt(__LINE__, (IMGUI_Style){ .flow = IMGUI_HORIZONTAL, .sizing[0] = IMGUI_SIZING_GROW }));
							{
								imgui_push_parent(imgui_widget_opt(__LINE__, (IMGUI_Style){ .sizing[0] = IMGUI_SIZING_GROW }));
								imgui_label(__LINE__, s("Ambient Strength"));
								imgui_pop_parent();

								imgui_push_parent(imgui_widget_opt(__LINE__, (IMGUI_Style){ .sizing[0] = IMGUI_SIZING_GROW }));
								imgui_sliderf(__LINE__, &ambient_strength, 0.0f, 1.0f);
								imgui_pop_parent();

								imgui_pop_parent();
							}

							Color selected = hex(0x3178c6);

							imgui_push_parent(imgui_widget_opt(__LINE__, (IMGUI_Style){ .flow = IMGUI_HORIZONTAL, .sizing[0] = IMGUI_SIZING_GROW }));
							{
								imgui_push_parent(imgui_widget_opt(__LINE__, (IMGUI_Style){ .sizing[0] = IMGUI_SIZING_GROW }));
								imgui_label(__LINE__, s("Light setting"));
								imgui_pop_parent();

								drop_down = imgui_widget_opt(__LINE__,
									(IMGUI_Style){
									  .sizing[0] = IMGUI_SIZING_GROW,
									  .bg = hex(0x262c36),
									  .p = 6.0f,
									  .border_radius = splat4(4.0f),
									});
								imgui_push_parent(drop_down);
								{
									imgui_label(__LINE__, light_setting_name[light_index]);
									imgui_spacer();
									imgui_label(__LINE__, s("^ "));
									IMGUI_Interact i = imgui_interact(drop_down->id, imgui_rect_cached(drop_down));
									if (i.pressed)
										dropdown_active = !dropdown_active;

									imgui_pop_parent();
								}

								imgui_pop_parent();
							}

							imgui_push_parent(imgui_widget_opt(__LINE__,
								(IMGUI_Style){
								  .flow = IMGUI_HORIZONTAL,
								  .sizing[0] = IMGUI_SIZING_GROW,
								  .gap = 8.0f,
								  .align[1] = IMGUI_ALIGN_CENTER,
								}));
							{
								{
									IMGUI_Widget *radio = imgui_widget_opt(__LINE__,
										(IMGUI_Style){
										  .sizing = { IMGUI_SIZING_GROW, IMGUI_SIZING_FIT },
										  .p = 8.0f,
										  .gap = 8.0f,
										  .border_radius = splat4(4.0f),
										  .gap = 8.0f,
										});

									IMGUI_Interact interact = imgui_interact(radio->id, imgui_rect_cached(radio));
									if (interact.pressed)
										light_index = 0;
									if (interact.hovered)
										radio->settings.bg = hex(0x262c36);

									imgui_push_parent(radio);
									{
										IMGUI_Widget *label = imgui_label(__LINE__, s("Day"));

										imgui_spacer();
										IMGUI_Widget *btn = imgui_widget_opt(0,
											(IMGUI_Style){
											  .sizing = { IMGUI_SIZING_FIXED, IMGUI_SIZING_FIXED },
											  .bg = light_index == 0 ? selected : WHITE,
											  .border_radius = splat4(imgui.default_font->bake_size * 0.5f), // circle
											});
										btn->size[0] = imgui.default_font->bake_size, btn->size[1] = imgui.default_font->bake_size;

										imgui_pop_parent();
									}
								}

								{
									IMGUI_Widget *radio = imgui_widget_opt(__LINE__,
										(IMGUI_Style){
										  .sizing = { IMGUI_SIZING_GROW, IMGUI_SIZING_FIT },
										  .align[1] = IMGUI_ALIGN_CENTER,
										  .p = 8.0f,
										  .gap = 8.0f,
										  .border_radius = splat4(4.0f),
										  .gap = 8.0f,
										});

									IMGUI_Interact interact = imgui_interact(radio->id, imgui_rect_cached(radio));
									if (interact.pressed)
										light_index = 1;
									if (interact.hovered)
										radio->settings.bg = hex(0x262c36);

									imgui_push_parent(radio);
									{
										IMGUI_Widget *label = imgui_label(__LINE__, s("Dawn"));

										imgui_spacer();
										IMGUI_Widget *btn = imgui_widget_opt(0,
											(IMGUI_Style){
											  .sizing = { IMGUI_SIZING_FIXED, IMGUI_SIZING_FIXED },
											  .bg = light_index == 1 ? selected : WHITE,
											  .border_radius = splat4(imgui.default_font->bake_size * 0.5f), // circle
											});
										btn->size[0] = imgui.default_font->bake_size, btn->size[1] = imgui.default_font->bake_size;

										imgui_pop_parent();
									}
								}

								{
									IMGUI_Widget *radio = imgui_widget_opt(__LINE__,
										(IMGUI_Style){
										  .sizing = { IMGUI_SIZING_GROW, IMGUI_SIZING_FIT },
										  .p = 8.0f,
										  .gap = 8.0f,
										  .border_radius = splat4(4.0f),
										  .gap = 8.0f,
										});

									IMGUI_Interact interact = imgui_interact(radio->id, imgui_rect_cached(radio));
									if (interact.pressed)
										light_index = 2;
									if (interact.hovered)
										radio->settings.bg = hex(0x262c36);

									imgui_push_parent(radio);
									{
										IMGUI_Widget *label = imgui_label(__LINE__, s("Night"));
										imgui_spacer();
										IMGUI_Widget *btn = imgui_widget_opt(0,
											(IMGUI_Style){
											  .sizing = { IMGUI_SIZING_FIXED, IMGUI_SIZING_FIXED },
											  .bg = light_index == 2 ? selected : WHITE,
											  .border_radius = splat4(imgui.default_font->bake_size * 0.5f), // circle
											});
										btn->size[0] = imgui.default_font->bake_size, btn->size[1] = imgui.default_font->bake_size;

										imgui_pop_parent();
									}
								}
							}

							imgui_pop_style(1);
							imgui_pop_parent();
						}

						imgui_pop_parent();
					}

					imgui_pop_parent();

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
								IMGUI_Widget *dock_preview = imgui_widget_opt(__LINE__,
									(IMGUI_Style){
									  .sizing = { IMGUI_SIZING_GROW, IMGUI_SIZING_GROW },
									  .bg = rgba(0, 128, 128, 48),
									});
								imgui_parent(dock_preview, root);
							} else {
								dock_orientation = dock_dir.x ? dock_dir.x < 0 ? IMGUI_DOCK_LEFT : IMGUI_DOCK_RIGHT : dock_dir.y < 0 ? IMGUI_DOCK_TOP
																																	 : IMGUI_DOCK_BOTTOM;
								IMGUI_Widget *container = imgui_widget_opt(__LINE__,
									(IMGUI_Style){
									  .flow = dock_dir.x ? IMGUI_HORIZONTAL : IMGUI_VERTICAL,
									  .sizing = { IMGUI_SIZING_GROW, IMGUI_SIZING_GROW },
									});
								imgui_parent(container, root);

								IMGUI_Widget *divisions[3] = { 0 };

								uint32_t preview_index = dock_orientation < IMGUI_DOCK_CENTER ? 0 : 2;
								for (uint32_t index = 0; index < 3; ++index) {
									divisions[index] = imgui_widget_opt(0, (IMGUI_Style){ .sizing = { IMGUI_SIZING_GROW, IMGUI_SIZING_GROW } });
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
							store2(panel_offset, panel->offset);

						if (panel->parent == 0) {
							imgui_fit_tree(panel);
							imgui_grow_tree(panel);
							imgui_position_tree(panel);
						}
					}

					imgui_fit_tree(root);
					imgui_grow_tree(root);
					imgui_position_tree(root);

					if (dropdown_active) {
						IMGUI_Widget *widget = imgui_widget_opt(0,
							(IMGUI_Style){
							  .flow = IMGUI_VERTICAL,
							  .bg = hex(0x262c36),
							  .border_radius = splat4(4.0f),
							});
						Rectangle c = imgui_rect_cached(drop_down);
						/* widget->offset[0] = c.x, widget->offset[1] = c.y + c.height; */
						widget->offset[0] = drop_down->offset[0], widget->offset[1] = drop_down->offset[1] + drop_down->size[1];
						widget->size[0] = drop_down->size[0];

						imgui_push_parent(widget);
						imgui_push_style((IMGUI_Style){ .fg = WHITE });
						for (uint32_t index = 0; index < countof(lights); ++index) {
							IMGUI_Widget *btn = imgui_widget_opt(hash64_combine(__LINE__, index),
								(IMGUI_Style){
								  .align[0] = IMGUI_ALIGN_CENTER,
								  .sizing = { IMGUI_SIZING_GROW, IMGUI_SIZING_GROW },
								  .flow = IMGUI_HORIZONTAL,
								  .p = 8.0f,
								});
							imgui_push_parent(btn);

							IMGUI_Interact i = imgui_interact(btn->id, imgui_rect_cached(btn));
							if (i.hovered)
								btn->settings.bg = hex(0x3178c6);
							if (i.released)
								light_index = index;

							imgui_label(0, light_setting_name[index]);
							imgui_pop_parent();
						}
						imgui_pop_style(1);
						imgui_pop_parent();

						imgui_layout(widget);
					}
				}
			} break;
			case VIEWPORT_STATE_GAME: { // :game
				os_cursor_capture(main_render, input_key_pressed(KEY_CODE_E) ? !os_cursor_captured(main_render) : os_cursor_captured(main_render));

				static uint32_t heart_count = 8;
				// :ui
				if (os_cursor_captured(main_render)) {
					IMGUI_Widget *heal_hurt_container = imgui_widget_opt(shash("heal_hurt_container"),
						(IMGUI_Style){
						  .sizing = { IMGUI_SIZING_FIXED, IMGUI_SIZING_FIT },
						  .align = { IMGUI_ALIGN_RIGHT, IMGUI_ALIGN_CENTER },
						  .p = 12.0f,
						  .gap = 8.0f,
						});
					heal_hurt_container->size[0] = viewport.width;

					imgui_push_parent(heal_hurt_container);
					{
						IMGUI_Style btn_style = {
							.p = 12.0f,
							.bg = hex(0x3b383d),
							.fg = WHITE,
						};

						imgui_push_style(btn_style);

						if (imgui_button_label(s("Heal VFX")).pressed) {
							LOG_INFO("Heal");
						}
						if (imgui_button_label(s("Hurt VFX")).pressed) {
							LOG_INFO("Hurt");
						}

						imgui_pop_style(1);
					}
					imgui_pop_parent();
					imgui_layout(heal_hurt_container);

					IMGUI_Widget *heart_container = imgui_widget_opt(__LINE__,
						(IMGUI_Style){
						  .flow = IMGUI_HORIZONTAL,
						  .sizing = { IMGUI_SIZING_GROW, IMGUI_SIZING_FIT },
						  .p = 12.0f,
						  .gap = 2.0f,
						});
					{
						IMGUI_Style style = {
							.fg = WHITE,
						};
						imgui_push_style(style);

						imgui_push_parent(heart_container);
						for (uint32_t index = 0; index < heart_count; ++index)
							imgui_image(hash64_combine(__LINE__, index), &heart_image, 0.13f);

						imgui_pop_style(1);
						imgui_pop_parent();
					}
					imgui_layout(heart_container);

					uint32_t x = 0;
					(void)x;
				} else {
				}

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
							if (len3_sq(velocity) <= EPSILON) break;
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

							draw3d_arrow(batch_line3d, nearest.point, add3(nearest.point, scale3(nearest.normal, 0.8f)), 3.0f, RED, view, proj, dims.x);
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

					for (uint32_t entity_index = 0; entity_index < scene->entity_count; ++entity_index) {
						Entity *entity = &scene->entities[entity_index];
						if (entity_has(entity, ENTITY_FEATURE_INTERACTABLE) == false || entity == player)
							continue;

						float3 offset = sub3(entity->transform.translation, player->transform.translation);
						float dist_sq = dot3(offset, offset);

						if (dist_sq <= (entity->interact_radius * entity->interact_radius)) {
							float3 interact_point = entity->transform.translation;
							if (entity_has(entity, ENTITY_FEATURE_DRAW_MESH)) { // make textbox above entity's head
								Mesh *mesh = &meshes[entity->meshid];

								float3 top_center = aabb3_center(mesh->bounds);
								top_center.y += aabb3_half_extent(mesh->bounds).y;

								interact_point = add3(interact_point, top_center);
							}

							float2 screen;
							if (project_to_viewport(view_proj, viewport, interact_point, &screen)) {
								screen.y -= 30;

								Rectangle textbox = rect_from_center(screen, make2(60.0f, 10.0f));

								draw2d_quad(batch_quad2d, textbox,
									(DRAW_QuadStyle){
									  .corner_radii = splat4(8.0f),
									  .border_width = 1.0f,
									  .border_color = BLACK,
									  .fill_color = WHITE,
									});

								Font *font = &fonts[FONT_IBM_PLEX_MONO][FONT_BAKE_SIZE_16];
								String8 text = s("Press F");

								float2 text_half_size = scale2(measure_text(font, text), 0.5f);
								float2 center = sub2(screen, text_half_size);
								draw2d_textf(batch_quad2d, font, center, BLACK, text);
							}

							if (dist_sq < closest) {
								closest = dist_sq;
								target = entity;
							}
						}
					}

					if (target && input_key_pressed(KEY_CODE_F))
						LOG_INFO("interaction with entity %u (%s)!", indexof(scene->entities, target), str8_filename(meshid_to_metadata[target->meshid]).text);
				}

			} break;
			default:
				break;
		}

		draw2d_line(batch_quad2d, splat2(100.0f), splat2(200.0f), 32.0f, RED);

		for (uint32_t index = 0; index < imgui.widget_count; ++index) {
			IMGUI_Widget *widget = &imgui.widgets[index];
			if (imgui_valid(widget)) {
				IMGUI_Widget *parent = &imgui.widgets[widget->parent];
				// TODO: Scissor
				/* if (imgui_valid(parent)) */
				/* 	BeginScissorMode(parent->offset[0], parent->offset[1], parent->size[0], parent->size[1]); */

				if (widget->settings.image) {
					draw2d_quad(batch_quad2d, imgui_rect_live(widget),
						(DRAW_QuadStyle){
						  .image = widget->settings.image,
						  .border_width = widget->settings.border_width,
						  .border_color = widget->settings.border,
						  .corner_radii = widget->settings.border_radius,
						  .fill_color = widget->settings.fg,
						});

				} else if (widget->settings.text.length) {
					/* draw2d_rect(batch_2d, imgui_rect_live(widget), ORANGE); */
					Font *font = widget->settings.font ? widget->settings.font : imgui.default_font;
					draw2d_textf(batch_quad2d, font, load2(widget->offset), widget->settings.fg, widget->settings.text);
				} else {
					draw2d_quad(batch_quad2d, imgui_rect_live(widget),
						(DRAW_QuadStyle){
						  .border_width = widget->settings.border_width,
						  .border_color = widget->settings.border,
						  .corner_radii = widget->settings.border_radius,
						  .fill_color = widget->settings.bg,
						});
				}
				/* if (imgui_valid(parent)) */
				/* 	EndScissorMode(); */
			}
		}
		imgui_frame_end();

		/* draw2d_quad( */
		/* 	batch_2d, */
		/* 	rect(24.0f, 24.0f, 200.0f, 200.0f), */
		/* 	(Rectangle){ 0 }, 0, (float2){ 0 }, */
		/* 	0.0f, 4.0f, RED, (float4){ 0.0f, 32.0f, 32.0f, 0.0f }, WHITE); */

		if (draw_collision_shapes) {
			for (uint32_t instance_index = 0; instance_index < scene->entity_count; ++instance_index) {
				Entity *entity = &scene->entities[instance_index];
				if (entity_has(entity, ENTITY_FEATURE_COLLIDABLE) == false)
					continue;

				Shape3 a = { 0 };
				if (entity->shape.kind == SHAPE_KIND_AABB3)
					a = shape3_from_aabb3(aabb3_from_center(aabb3_center(entity->shape.as.aabb3), mul3(aabb3_half_extent(entity->shape.as.aabb3), entity->transform.scale)));

				draw3d_shape_outline(batch_line3d, entity->shape.kind == SHAPE_KIND_AABB3 ? &a : &entity->shape, entity->transform.translation, 3.0f);
			}
		}

		GFX_Command *cmd = gfx_frame_begin(device);
		if (cmd == 0)
			continue;

		// Swapchain image acquisition
		GFX_Image *compute_blit_target = os_surface_drawable(popup_compute) ? gfx_swapchain_backbuffer(device, cmd, popup_swapchain) : 0;
		if (compute_blit_target) {
			ASSERT(compute_blit_target->width == compute_image->width && compute_blit_target->height == compute_image->height);
			// transition swapchain target & blit src compute image
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_TRANSFER_DST, compute_blit_target);
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_COMPUTE_SHADER_WRITE, compute_image);

			gfx_cmd_shader_bind(cmd, shaders[SHADER_TEST_COMPUTE]);
			gfx_cmd_bind(device, 0, array_arg(Uniform, storage_images(0, (GFX_Image *[]){ compute_image }, 1)));

			// Dispatch compute & Blit to main window surface
			struct {
				float2 mouse;
				float time;
			} pc = {
				.mouse = compute_mouse,
				.time = (float)time,
			};

			gfx_cmd_push_constant(cmd, sizeof(pc), &pc);
			gfx_cmd_dispatch(cmd, (compute_blit_target->width / 16) + 1, (compute_blit_target->height / 16) + 1, 1);
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_TRANSFER_SRC, compute_image);
			Rectangle area = rect(0, 0, compute_blit_target->width, compute_blit_target->height);
			gfx_cmd_image_blit(cmd, area, compute_image, area, compute_blit_target);
		}

		GFX_Image *main_target = gfx_swapchain_backbuffer(device, cmd, main_swapchain);
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

				gfx_cmd_shader_bind(cmd, shaders[SHADER_SKINNING_COMPUTE]);
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
				gfx_cmd_push_constant(cmd, sizeof(pc), &pc);

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
				lookat(make3_from4(lights[light_index].position), splat3(0.0f), unit3(UP)));

			Frame3D frame_data = {
				.viewport = { dims.x, dims.y },
				.ambient_strength = ambient_strength,
				.fog_density = fog_density,
				.fog_gradient = fog_gradient,
				.time = time,
			};

			{ // :shadow
				gfx_cmd_draw_begin(cmd,
					(GFX_DrawPassInfo){
					  .debug_name = "SHADOW_PASS",
					  .depth = { shadow_depthbuffer, LOAD_OP_CLEAR, STORE_OP_STORE, .clear = 1.0f },
					  .area = { 0.0f, 0.0f, shadow_depthbuffer->width, shadow_depthbuffer->height },
					});

				frame_data.view = lights[light_index].matrix;
				frame_data.proj = identity4x4();
				frame_data.camera_position = lights[light_index].position;
				frame_data.proj.elements[5] *= -1;

				gfx_cmd_shader_bind(cmd, shaders[SHADER_SHADOW]);
				gfx_cmd_bind(device, 0, array_arg(Uniform, uniform_data(0, &frame_data, sizeof(frame_data))));

				// :shadow
				for (uint32_t instance_index = 0; instance_index < scene->entity_count; ++instance_index) {
					Entity *entity = &scene->entities[instance_index];
					if (entity_has(entity, ENTITY_FEATURE_CAST_SHADOW) == false)
						continue;

					Mesh *mesh = &meshes[entity->meshid];

					gfx_cmd_bind_index_buffer32(cmd, geometry, mesh->buffer_index_byte_offset);
					for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
						MeshPart *part = &mesh->parts[part_index];
						Material *material = &mesh->materials[part->material_id];

						float4x4 transform = compose4x4_quat(
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

						gfx_cmd_bind(device, 1, array_arg(Uniform, storage_buffers(0, buffer, offset, size)));

						gfx_cmd_push_constant(cmd, sizeof(pc), &pc);
						gfx_cmd_draw_indexed(cmd, part->index_offset, part->index_count, part->vertex_offset);
					}
				}

				gfx_cmd_draw_end(cmd);
			}

			{ // :spatial
				gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, shadow_depthbuffer);
				gfx_cmd_draw_begin(cmd,
					(GFX_DrawPassInfo){
					  .debug_name = "SPATIAL_PASS",
					  .colors[0] = { msaa_target, spatial_target, LOAD_OP_CLEAR, STORE_OP_STORE, .clear = RED },
					  .depth = { depthbuffer, LOAD_OP_CLEAR, STORE_OP_STORE, .clear = 1.0f },
					});

				frame_data.view = camera_view(camera);
				frame_data.proj = camera_proj(camera, viewport.width / viewport.height);
				frame_data.camera_position = make4_from3(camera->position, 0.0f);

				Uniform uniforms[] = {
					uniform_data(0, &frame_data, sizeof(frame_data)),
					storage_data(1, lights + light_index, sizeof(lights[0])),
					sampler_with_textures(2, (GFX_Image *[]){ shadow_depthbuffer }, 1, shadow_sampler),
					sampler_with_textures(3, (GFX_Image *[]){ skybox.handle }, 1, linear_sampler[WRAP_MODE_CLAMP]),
				};
				gfx_cmd_bind(device, 0, uniforms, countof(uniforms));

				// :scene
				gfx_cmd_shader_bind(cmd, shaders[SHADER_SPATIAL]);
				for (uint32_t instance_index = 0; instance_index < scene->entity_count; ++instance_index) {
					Entity *entity = &scene->entities[instance_index];
					if (entity_has(entity, ENTITY_FEATURE_DRAW_MESH) == false || entity_has(entity, ENTITY_FEATURE_TRANSPARENT))
						continue;

					Mesh *mesh = &meshes[entity->meshid];

					gfx_cmd_bind_index_buffer32(cmd, geometry, mesh->buffer_index_byte_offset);
					for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
						MeshPart *part = &mesh->parts[part_index];
						Material *material = &mesh->materials[part->material_id];

						float4x4 transform = compose4x4_quat(
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
							.emissive = splat4(1.0f),
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
						gfx_cmd_bind(device, 1, uniforms, countof(uniforms));

						gfx_cmd_push_constant(cmd, sizeof(pc), &pc);
						gfx_cmd_draw_indexed(cmd, part->index_offset, part->index_count, part->vertex_offset);
					}
				}

				// :grass
				if (draw_grass) {
					Mesh *mesh = &meshes[MESH_GRASS_BILLBOARD];
					gfx_cmd_shader_bind(cmd, shaders[SHADER_GRASS]);

					gfx_cmd_bind_index_buffer32(cmd, geometry, mesh->buffer_index_byte_offset);
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
							.emissive = splat4(1.0f),
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
						gfx_cmd_bind(device, 1, uniforms, countof(uniforms));

						gfx_cmd_push_constant(cmd, sizeof(pc), &pc);
						gfx_cmd_draw_indexed_instanced(cmd, part->index_offset, part->index_count, part->vertex_offset, 0, map_width * map_depth);
					}
				}

				if (draw_skybox) { // :skybox
					gfx_cmd_shader_bind(cmd, shaders[SHADER_SKYBOX]);
					gfx_cmd_draw(cmd, 36, 0);
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
						if (entity_has(entity, ENTITY_FEATURE_DRAW_MESH) == false || entity_has(entity, ENTITY_FEATURE_TRANSPARENT) == false)
							continue;

						Mesh *mesh = &meshes[entity->meshid];
						float3 center = add3(entity->transform.translation, aabb3_center(mesh->bounds));

						transparent_meshes[transparent_mesh_count++] = (MeshSort){
							.distance = len3_sq(sub3(camera->position, center)),
							.entity = entity
						};
					}
					arena_pop(frame_arena, sizeof(MeshSort) * (scene->entity_count - transparent_mesh_count));

					qsort(transparent_meshes, transparent_mesh_count, sizeof(MeshSort), cmp_mesh_sort);

					if (transparent_mesh_count)
						gfx_cmd_shader_bind(cmd, shaders[SHADER_TRANSPARENT]);

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

						gfx_cmd_bind(device, 1, uniforms, countof(uniforms));

						gfx_cmd_bind_index_buffer32(cmd, geometry, mesh->buffer_index_byte_offset);
						for (uint32_t part_index = 0; part_index < mesh->part_count; ++part_index) {
							MeshPart *part = &mesh->parts[part_index];

							struct {
								float4x4 model;
								float4 tint;
								float4 emissive;
								float2 metallic_roughness;
								float4 uv_st;
							} pc = {
								.model = compose4x4_quat(e->transform.translation, e->transform.rotation, e->transform.scale),
								.tint = mesh->materials[part->material_id].tint,
								.emissive = mesh->materials[part->material_id].tint,
							};
							float4x4 world_from_object = compose4x4_quat(e->transform.translation, e->transform.rotation, e->transform.scale);

							gfx_cmd_push_constant(cmd, sizeof(world_from_object), world_from_object.elements);
							gfx_cmd_draw_indexed(cmd, part->index_offset, part->index_count, part->vertex_offset);
						}
					}
				}

				{ // :overlay
					if (batch_line3d->offset) {
						gfx_cmd_shader_bind(cmd, shaders[SHADER_LINE3D]);
						Uniform uniforms[] = {
							storage_data(0, batch_line3d->base, batch_line3d->offset),
						};
						gfx_cmd_bind(device, 1, uniforms, countof(uniforms));
						gfx_cmd_draw(cmd, (batch_line3d->offset / sizeof(DRAW_Line3D)) * 6, 0);
					}
				}

				gfx_cmd_draw_end(cmd);
			}

			if (batch_quad2d->offset) { // :canvas

				Frame3D frame_2d = {
					.view = identity4x4(),
					.proj = orthographic(0.0f, dims.x, 0.0f, dims.y, -50.f, 50.f),
					.viewport = cast2(dims, float2),
					.time = time,
				};
				uint32_t quad_count = batch_quad2d->offset / sizeof(DRAW_Quad2D);

				GFX_Image *images[32] = { 0 };
				uint32_t image_count = 1;
				for (uint32_t texture_id = 0; texture_id < 32; ++texture_id)
					images[texture_id] = white_texture;

				for (uint32_t quad_instance = 0; quad_instance < quad_count; ++quad_instance) {
					DRAW_Quad2D *quad = (DRAW_Quad2D *)batch_quad2d->base + quad_instance;

					if (quad->imageid && quad->imageid != indexof(device->image_pool, white_texture)) {
						int32_t found_index = -1;
						for (uint32_t image_index = 1; image_index < image_count; ++image_index) {
							if (indexof(device->image_pool, images[image_index]) == quad->imageid) {
								found_index = image_index;
								break;
							}
						}

						if (found_index == -1) {
							ASSERT(image_count < countof(images) && "Extend sprite batching to support beyond 32 distinct images");
							found_index = image_count++;
							images[found_index] = &device->image_pool[quad->imageid];
							gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, device->image_pool + quad->imageid);
						}

						quad->imageid = found_index;
					}
				}

				gfx_cmd_draw_begin(cmd,
					(GFX_DrawPassInfo){
					  .debug_name = "UI_PASS",
					  .colors[0] = {
						.target = ui_target,
						.clear = TRANSPARENT,
						.load = LOAD_OP_CLEAR,
						.store = STORE_OP_STORE,
					  },
					});

				gfx_cmd_shader_bind(cmd, shaders[SHADER_QUAD2D]);

				Uniform uniforms0[] = {
					uniform_data(0, &frame_2d, sizeof(frame_2d)),
					storage_data(1, batch_quad2d->base, batch_quad2d->offset),
				};
				Uniform uniforms1[] = { sampler_with_textures(0, images, countof(images), nearest_sampler[WRAP_MODE_CLAMP]) };

				gfx_cmd_bind(device, 0, uniforms0, countof(uniforms0));
				gfx_cmd_bind(device, 1, uniforms1, countof(uniforms1));

				gfx_cmd_draw_instanced(cmd, 0, 6, 0, quad_count);
				gfx_cmd_draw_end(cmd);
			} else
				gfx_cmd_image_clear(cmd, (Rectangle){ 0 }, TRANSPARENT, ui_target);

			{ // :composite
				gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, spatial_target);
				gfx_cmd_image_transition(cmd, RESOURCE_USAGE_SHADER_READ, ui_target);

				gfx_cmd_draw_begin(cmd,
					(GFX_DrawPassInfo){
					  .debug_name = "COMPOSITE_PASS",
					  .colors[0] = { main_target, 0, LOAD_OP_CLEAR, STORE_OP_STORE, .clear = RED },
					});
				gfx_cmd_shader_bind(cmd, shaders[SHADER_COMPOSITE]);

				GFX_Image *images[] = {
					spatial_target,
					ui_target,
				};

				Uniform uniforms[] = {
					sampler_with_textures(0, images, countof(images), linear_sampler[WRAP_MODE_CLAMP]),
				};

				gfx_cmd_bind(device, 0, uniforms, countof(uniforms));

				gfx_cmd_draw(cmd, 6, 0);
				gfx_cmd_draw_end(cmd);
			}
		}
		gfx_frame_end(device, cmd);

		for (ShaderID ID = 0; ID < SHADER_MAX; ++ID) { // :hot-reload
			ShaderMetadata *metadata = &shaderid_to_metadata[ID];
			if (metadata->filepaths[SHADER_STAGE_VERTEX].length == 0 &&
				metadata->filepaths[SHADER_STAGE_FRAGMENT].length == 0 &&
				metadata->filepaths[SHADER_STAGE_COMPUTE].length == 0)
				continue;

			bool is_compute = metadata->filepaths[SHADER_STAGE_COMPUTE].length > 0;
			if (is_compute) {
				OS_Timestamp now = os_file_last_modified(metadata->filepaths[SHADER_STAGE_COMPUTE]);

				if (now != shader_ts[ID]) {
					ArenaTemp scratch = arena_scratch_begin(NULL);

					LOG_INFO("hot-reloading %s...", shaders[ID]->debug_name);
					vkDeviceWaitIdle(device->handle);

					gfx_shader_destroy(device, shaders[ID]);
					String8 bytecode = os_file_read_entire(scratch.arena, metadata->filepaths[SHADER_STAGE_COMPUTE]);
					shaders[ID] = gfx_compute_make(device, bytecode, (char *)shaderid_to_string[ID].text);

					shader_ts[ID] = now;
					arena_scratch_end(scratch);
				}
			} else {
				OS_Timestamp fs_ts = os_file_last_modified(metadata->filepaths[SHADER_STAGE_FRAGMENT]);
				OS_Timestamp vs_ts = os_file_last_modified(metadata->filepaths[SHADER_STAGE_VERTEX]);

				OS_Timestamp now = MAX(fs_ts, vs_ts);
				if (now != shader_ts[ID]) {
					LOG_INFO("hot-reloading %s...", shaders[ID]->debug_name);
					vkDeviceWaitIdle(device->handle);
					ShaderMetadata *metadata = &shaderid_to_metadata[ID];

					gfx_shader_destroy(device, shaders[ID]);
					ArenaTemp scratch = arena_scratch_begin(NULL);

					String8 vs_bytecode = os_file_read_entire(scratch.arena, metadata->filepaths[SHADER_STAGE_VERTEX]);
					String8 fs_bytecode = os_file_read_entire(scratch.arena, metadata->filepaths[SHADER_STAGE_FRAGMENT]);
					shaders[ID] = gfx_shader_make(device, vs_bytecode, fs_bytecode, (char *)shaderid_to_string[ID].text);

					for (uint32_t permutation = 0; permutation < metadata->pipeline_count; ++permutation)
						gfx_pipeline_ensure(device, shaders[ID], metadata->pipelines[permutation]);

					shader_ts[ID] = now;
					arena_scratch_end(scratch);
				}
			}
		}

		device->current_frame_index = (device->current_frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
		arena_reset(frame_arena);
	}

	gfx_device_destroy(device);
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
			uint8_t *buffer_data = (uint8_t *)cgltf_buffer_view_data(image->buffer_view);
			uint32_t channels = 0;
			result.pixels = stbi_load_from_memory(buffer_data, image->buffer_view->size, (int32_t *)&result.width, (int32_t *)&result.height, (int32_t *)&channels, 4);
			result.format = PIXEL_FORMAT_RGBA8_SRGB;
		}
	}

	arena_scratch_end(scratch);
	return result;
}

Mesh load_gltf(Arena *arena, String8 path) {
	LOG_INFO("loading [%s] geometry.", path.text);

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
			.tint = splat4(1.0f),
		};

		for (uint32_t material_index = 0; material_index < data->materials_count; ++material_index) {
			cgltf_material *material = &data->materials[material_index];
			Material *out = &result.materials[material_index + 1];
			out->tint = (float4){ 1.0f, 1.0f, 1.0f, 1.0f };

			if (material->has_pbr_metallic_roughness) {
				cgltf_pbr_metallic_roughness *pbr = &material->pbr_metallic_roughness;

				out->tint = load4(pbr->base_color_factor);
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
								part->bounds.min = load3(accessor->min);
								part->bounds.max = load3(accessor->max);
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

AnimationClip *load_gltf_animations(Arena *arena, String8 path, uint32_t *count) {
	LOG_INFO("loading [%s] animations.", path.text);

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
			.emissive = splat4(1.0f),
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
			.emissive = splat4(1.0f),
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
	result.bounds = aabb3_empty();

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

				aabb3_expand(&result.bounds, vertex_cursor->position);
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
			.emissive = splat4(1.0f),
			.metallic_roughness = { 0.0f, 0.5f },
		};
	}

	return result;
}

Mesh mesh_heightmap(Arena *arena, Side orientation, float w, float h, Image2D heightmap) {
	Mesh result = { 0 };
	result.bounds = aabb3_empty();

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

				aabb3_expand(&result.bounds, result.vertices[index].position);
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
			.emissive = splat4(1.0f),
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
