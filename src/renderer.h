#pragma once

#include "assets/asset_types.h"
#include "core/identifiers.h"

#include "scene.h"

typedef Handle HMaterial;
typedef Handle HMesh;

bool renderer_system_startup(void *memory, size_t size, void *display, uint32_t width, uint32_t height);
void renderer_system_shutdown(void);

bool renderer_begin_frame(Camera *camera);
bool renderer_draw_mesh(Handle mesh, Handle material_instance, mat4 transform);
bool renderer_end_frame(void);

bool renderer_on_resize(uint32_t width, uint32_t height);

Handle renderer_image_upload(UUID id, ImageSource *source);
// Handle renderer_texture_upload(UUID id, ImageSource *source );

Handle renderer_mesh_upload(UUID id, MeshSource *mesh);

Handle renderer_material_base_upload(UUID id, MaterialSource *source);

Handle renderer_material_instance_create(Handle base);
bool renderer_material_instance_destroy(Handle instance);

bool renderer_material_instance_setf(Handle instance, String name, float value);
bool renderer_material_instance_set2fv(Handle instance, String name, vec2 value);
bool renderer_material_instance_set3fv(Handle instance, String name, vec3 value);
bool renderer_material_instance_set4fv(Handle instance, String name, vec4 value);

bool renderer_material_instance_set_texture(Handle instance, String name, UUID texture);
// bool renderer_material_instance_commit(Handle instance);
