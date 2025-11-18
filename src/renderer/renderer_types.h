#pragma once

#include "common.h"

#include <cglm/cglm.h>

// Shader
typedef void *Shader;

typedef struct {
	char *path;
	uint32_t width, height, channels;

	void *internal;
} Texture;

typedef struct material {
	Texture base_color_texture;
	Texture metallic_roughness_texture;

	float base_color_factor[4];
	float metallic_factor;
	float roughness_factor;
} Material;

typedef struct primitive {
	vec3 *positions;
	vec3 *normals;
	vec4 *tangets;
	vec2 *uvs;

	uint32_t *indices;

	uint32_t vertex_count, index_count;

	Material material;
} RenderPrimitive;

typedef struct model {
	RenderPrimitive *primitives;
	uint32_t primitive_count;
} Model;
