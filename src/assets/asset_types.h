#pragma once

#include "common.h"
#include "core/astring.h"
#include "renderer/renderer_types.h"

#include <cglm/cglm.h>

typedef enum {
	ASSET_TYPE_UNDEFINED,
	ASSET_TYPE_GEOMETRY,
	ASSET_TYPE_IMAGE,
	ASSET_TYPE_COUNT,
} AssetType;

typedef struct image {
	UUID asset_id;
	String path;

	void *pixels;
	int32_t width, height, channels;
} Image;

typedef struct material_source {
	UUID asset_id;
	Image *base_color_texture;
	Image *metallic_roughness_texture; // G = Roughness, B = Metallic
	Image *normal_texture;
	Image *occlusion_texture;
	Image *emissive_texture;

	vec4 base_color_factor;
	float metallic_factor;
	float roughness_factor;
	vec3 emissive_factor;
} MaterialSource;

typedef struct mesh_source {
	UUID asset_id;

	Vertex *vertices;
	uint32_t vertex_count;

	uint32_t *indices;
	uint32_t index_count;

	MaterialSource *material;
} MeshSource;

typedef struct model_source {
	MeshSource *meshes;
	uint32_t mesh_count;

	MaterialSource *materials;
	uint32_t material_count;

	Image *images;
	uint32_t image_count;
} ModelSource;
