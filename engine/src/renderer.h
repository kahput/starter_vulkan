// #pragma once
//
// #include "core/r_types.h"
// #include "renderer/r_internal.h"
//
// #include "scene.h"
//
// #include "core/identifiers.h"
//
typedef enum {
	RENDERER_DEFAULT_TEXTURE_WHITE = 1,
	RENDERER_DEFAULT_TEXTURE_BLACK,
	RENDERER_DEFAULT_TEXTURE_NORMAL,
	RENDERER_DEFAULT_TEXTURE_SKYBOX,
	RENDERER_DEFAULT_TARGET_SHADOW_DEPTH_MAP,
	RENDERER_DEFAULT_TARGET_MAIN_DEPTH_MAP,
	RENDERER_DEFAULT_TARGET_MAIN_COLOR_MAP,
	RENDERER_DEFAULT_SURFACE_COUNT,
} TextureIndex;

typedef enum {
	RENDERER_DEFAULT_SAMPLER_LINEAR = 1,
	RENDERER_DEFAULT_SAMPLER_NEAREST,
} SamplerIndex;

typedef enum {
	RENDERER_DEFAULT_SHADER_VARIANT_STANDARD = 1,
	RENDERER_DEFAULT_SHADER_VARIANT_WIREFRAME
} ShaderVariant;

typedef enum {
	RENDERER_DEFAULT_PASS_SHADOW = 1,
	RENDERER_DEFAULT_PASS_MAIN,
	RENDERER_DEFAULT_PASS_POSTFX
} RenderPassIndex;

typedef enum {
	RENDERER_GLOBAL_RESOURCE_SHADOW = 1,
	RENDERER_GLOBAL_RESOURCE_MAIN,
	RENDERER_GLOBAL_RESOURCE_POSTFX,
} GlobalResourceIndex;
//
// bool renderer_system_startup(void *memory, size_t size, void *display, uint32_t width, uint32_t height);
// void renderer_system_shutdown(void);
//
// bool renderer_frame_begin(Camera *camera, uint32_t point_light_count, Light *lights);
// bool renderer_frame_end(void);
//
// // bool renderer_pass_begin(RenderPass pass);
// // bool renderer_pass_end(void);
//
// bool renderer_draw_mesh(RMesh mesh_handle, RMaterial material, uint32_t material_instance, mat4 transform);
//
// void renderer_state_global_wireframe_set(bool active);
//
// bool renderer_on_resize(uint32_t width, uint32_t height);
//
// RTexture renderer_texture_create(UUID id, TextureConfig *config);
// bool renderer_texture_destroy(RTexture);
//
// RMesh renderer_mesh_create(UUID id, MeshConfig *config);
// bool renderer_mesh_destroy(RMesh mesh);
//
// RShader renderer_shader_create(UUID id, ShaderConfig *config);
// RShader renderer_shader_default(void);
// bool renderer_shader_destroy(RShader shader);
//
// RMaterial renderer_material_create(RShader shader, uint32_t parameter_count, ShaderParameter *parameters);
// RMaterial renderer_material_default(void);
// bool renderer_material_destroy(RMaterial material);
//
// bool renderer_material_setf(RMaterial material, String name, float value);
// bool renderer_material_set2fv(RMaterial material, String name, vec2 value);
// bool renderer_material_set3fv(RMaterial material, String name, vec3 value);
// bool renderer_material_set4fv(RMaterial material, String name, vec4 value);
// bool renderer_material_set_texture(RMaterial material, String name, UUID texture); // TODO: Bindless descriptors
//
// bool renderer_material_instance_setf(RMaterial material, uint32_t material_instance, String name, float value);
// bool renderer_material_instance_set2fv(RMaterial material, uint32_t material_instance, String name, vec2 value);
// bool renderer_material_instance_set3fv(RMaterial material, uint32_t material_instance, String name, vec3 value);
// bool renderer_material_instance_set4fv(RMaterial material, uint32_t material_instance, String name, vec4 value);
//
// // TODO: Maybe override on mesh instance basis
// // bool renderer_mesh_override_setf(RMesh mesh, String name, float value);
// // bool renderer_mesh_override_set2fv(RMesh mesh, String name, vec2 value);
// // bool renderer_mesh_override_set3fv(RMesh mesh, String name, vec3 value);
// // bool renderer_mesh_override_set4fv(RMesh mesh, String name, vec4 value);
// // bool renderer_mesh_override_set_texture(RMesh mesh, String name, UUID texture); // TODO: Bindless descriptors
