#pragma once

#include "assets/asset_types.h"
#include "core/identifiers.h"

#include "renderer/r_internal.h"
#include "scene.h"

typedef Handle RTexture;
typedef Handle RShader;
typedef Handle RMaterial;
typedef Handle RMesh;

bool renderer_system_startup(void *memory, size_t size, void *display, uint32_t width, uint32_t height);
void renderer_system_shutdown(void);

bool renderer_begin_frame(Camera *camera);
bool renderer_draw_mesh(RMesh mesh_handle, RMaterial material_instance_handle, mat4 transform);
bool renderer_end_frame(void);

void renderer_state_global_wireframe_set(bool active);

bool renderer_on_resize(uint32_t width, uint32_t height);

typedef struct {
	void *pixels;
	uint32_t width;
	uint32_t height;
	uint32_t channels;
	bool is_srgb;
} TextureConfig;
RTexture renderer_texture_create(UUID id, TextureConfig *config);
bool renderer_texture_destroy(RTexture);

typedef struct {
	void *vertices;
	uint32_t vertex_size;
	uint32_t vertex_count;

	void *indices;
	uint32_t index_size;
	uint32_t index_count;
} MeshConfig;
RMesh renderer_mesh_create(UUID id, MeshConfig *config);
bool renderer_mesh_destroy(RMesh mesh);

typedef struct {
	void *vertex_code;
	size_t vertex_code_size;

	void *fragment_code;
	size_t fragment_code_size;

	// PipelineDesc pipeline_desc;

	void *default_ubo_data;
	size_t ubo_size;
} ShaderConfig;
RShader renderer_shader_create(UUID id, ShaderConfig *config);
bool renderer_shader_destroy(RShader shader);

RMaterial renderer_material_create(RShader base);
RMaterial renderer_material_default(void);
bool renderer_material_destroy(RMaterial material);

bool renderer_material_setf(RMaterial material, String name, float value);
bool renderer_material_set2fv(RMaterial material, String name, vec2 value);
bool renderer_material_set3fv(RMaterial material, String name, vec3 value);
bool renderer_material_set4fv(RMaterial material, String name, vec4 value);

bool renderer_material_set_texture(Handle instance, String name, UUID texture);
// bool renderer_material_instance_commit(Handle instance);
