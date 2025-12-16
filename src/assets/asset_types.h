#pragma once

#include "common.h"
#include "core/astring.h"
#include "renderer/renderer_types.h"

#include <cglm/cglm.h>

// TODO: Create ID for every asset
typedef uint64_t AssetID;

typedef struct texture_source {
	void *pixels;

	int32_t width, height, channels;
	String path;
} TextureSource;

typedef struct material_source {
	TextureSource *base_color_texture;
	TextureSource *metallic_roughness_texture; // G = Roughness, B = Metallic
	TextureSource *normal_texture;
	TextureSource *occlusion_texture;
	TextureSource *emissive_texture;

	vec4 base_color_factor;
	float metallic_factor;
	float roughness_factor;
	vec3 emissive_factor;
} MaterialSource;

typedef struct mesh_source {
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

	TextureSource *textures;
	uint32_t texture_count;
} ModelSource;
