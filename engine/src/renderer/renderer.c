#include "renderer.h"
#include "core/r_types.h"
#include "platform.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"
//
// #include "core/arena.h"
// #include "core/pool.h"
//
// #include "common.h"
// #include "core/astring.h"
// #include "core/debug.h"
// #include "core/logger.h"
// #include "core/hash_trie.h"
// #include "core/identifiers.h"
//
// #include "platform/filesystem.h"
//
// #include <math.h>
// #include <stdint.h>
// #include <string.h>
//
// typedef enum {
// 	RENDER_TARGET_TEXTURE_SHADOW_DEPTH = RENDERER_DEFAULT_TEXTURE_COUNT,
// 	RENDER_TARGET_TEXTURE_MAIN_DEPTH,
// 	RENDER_TARGET_TEXTURE_MAIN_COLOR,
//
// 	RENDER_TARGET_TEXTURE_COUNT,
// } DepthAttachment;
//
// enum {
// 	SHADOW_SAMPLER = RENDERER_DEFAULT_SAMPLER_COUNT
// };
//
// enum {
// 	GLOBAL_VIEW,
// 	SHADOW_VIEW,
// };
//
// typedef struct {
// 	HashTrieNode node;
//
// 	uint32_t pool_index;
// } ResourceEntry;
//
// typedef struct model {
// 	uint32_t mesh_pool_indices[32];
// 	uint32_t mesh_count;
//
// 	uint32_t image_pool_indices[32];
// 	uint32_t image_count;
//
// 	uint32_t material_pool_indices[16];
// 	uint32_t material_count;
// } Model;
//
// typedef struct renderer {
// 	Arena *arena;
// 	VulkanContext *context;
// 	Platform *display;
//
// 	uint32_t width, height;
//
// 	RShader default_shader;
// 	RMaterial default_material;
//
// 	ResourceEntry *texture_map;
// 	Pool *texture_allocator;
//
// 	ResourceEntry *material_map;
// 	Pool *shader_allocator;
// 	Pool *material_allocator;
//
// 	ResourceEntry *mesh_map;
// 	Pool *mesh_allocator;
//
// 	IndexRecycler shader_indices;
// 	IndexRecycler buffer_indices;
// 	IndexRecycler image_indices;
// 	IndexRecycler sampler_indices;
//
// 	IndexRecycler group_indices;
// 	IndexRecycler global_indices;
// } Renderer;
//
// static Renderer *renderer = { 0 };
//
// static ShaderMember *shader_reflection_find_member(ShaderReflection *reflection, String name, ShaderUniformFrequency frequency);
// // static uint32_t shader_reflection_find_binding(ShaderReflection *reflection, String name, ShaderUniformFrequency frequency);
//
// static uint32_t resolve_mesh(UUID id, MeshConfig *config);
// static uint32_t resolve_texture(UUID id, TextureConfig *config);
// static uint32_t resolve_shader(UUID id, ShaderConfig *config);
//
// bool renderer_system_startup(void *memory, size_t size, void *display, uint32_t width, uint32_t height) {
// 	size_t minimum_footprint = sizeof(Renderer) + sizeof(Arena);
//
// 	uintptr_t base_addr = (uintptr_t)memory;
// 	uintptr_t aligned_addr = (uintptr_t)aligned_address((uintptr_t)memory, alignof(Arena));
// 	size_t padding = aligned_addr - base_addr;
//
// 	if (size < minimum_footprint + padding) {
// 		LOG_ERROR("Renderer: Failed to create renderer, memory footprint too small");
// 		ASSERT(false);
// 		return false;
// 	}
// 	Arena *arena = (Arena *)aligned_addr;
// 	*arena = (Arena){
// 		.memory = arena + 1,
// 		.offset = 0,
// 		.capacity = size - sizeof(Arena)
// 	};
//
// 	renderer = arena_push_struct_zero(arena, Renderer);
// 	renderer->arena = arena;
//
// 	renderer->display = display;
// 	renderer->width = width, renderer->height = height;
//
// 	if (vulkan_renderer_create(renderer->arena, renderer->display, &renderer->context) == false) {
// 		LOG_ERROR("Renderer: Failed to create vulkan context");
// 		ASSERT(false);
// 		return false;
// 	}
//
// 	index_recycler_create(renderer->arena, &renderer->shader_indices, 0, MAX_SHADERS);
// 	index_recycler_create(renderer->arena, &renderer->buffer_indices, 0, MAX_BUFFERS);
// 	index_recycler_create(renderer->arena, &renderer->image_indices, 0, MAX_TEXTURES);
// 	index_recycler_create(renderer->arena, &renderer->sampler_indices, 0, MAX_SAMPLERS);
//
// 	index_recycler_create(renderer->arena, &renderer->group_indices, 0, MAX_GROUP_RESOURCES);
// 	index_recycler_create(renderer->arena, &renderer->global_indices, SHADOW_VIEW, MAX_GLOBAL_RESOURCES);
//
// 	renderer->mesh_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(Mesh), alignof(Mesh));
// 	renderer->texture_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(Texture), alignof(Texture));
// 	renderer->shader_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(Shader), alignof(Shader));
// 	renderer->material_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(Material), alignof(Material));
//
// 	// ========================= DEFAULT ======================================
// 	uint8_t WHITE[4] = { 255, 255, 255, 255 };
// 	renderer_texture_create(identifier_generate(), (TextureConfig[]){ (TextureConfig){ .width = 1, .height = 1, .channels = 4, .is_srgb = true, .pixels = WHITE } });
// 	// vulkan_renderer_texture_create(renderer->context, RENDERER_DEFAULT_TEXTURE_WHITE, 1, 1, 4, true, TEXTURE_USAGE_SAMPLED, WHITE);
//
// 	uint8_t BLACK[4] = { 0, 0, 0, 255 };
// 	renderer_texture_create(identifier_generate(), (TextureConfig[]){ (TextureConfig){ .width = 1, .height = 1, .channels = 4, .is_srgb = true, .pixels = BLACK } });
// 	// vulkan_renderer_texture_create(renderer->context, RENDERER_DEFAULT_TEXTURE_BLACK, 1, 1, 4, true, TEXTURE_USAGE_SAMPLED, BLACK);
//
// 	uint8_t FLAT_NORMAL[4] = { 128, 128, 255, 255 };
// 	renderer_texture_create(identifier_generate(), (TextureConfig[]){ (TextureConfig){ .width = 1, .height = 1, .channels = 4, .is_srgb = false, .pixels = FLAT_NORMAL } });
// 	// vulkan_renderer_texture_create(renderer->context, RENDERER_DEFAULT_TEXTURE_NORMAL, 1, 1, 4, false, TEXTURE_USAGE_SAMPLED, FLAT_NORMAL);
//
// 	vulkan_renderer_sampler_create(renderer->context, RENDERER_DEFAULT_SAMPLER_LINEAR, LINEAR_SAMPLER);
// 	vulkan_renderer_sampler_create(renderer->context, RENDERER_DEFAULT_SAMPLER_NEAREST, NEAREST_SAMPLER);
//
// 	recycler_new_index(&renderer->image_indices);
// 	vulkan_renderer_texture_create(
// 		renderer->context, RENDER_TARGET_TEXTURE_MAIN_DEPTH,
// 		MATCH_SWAPCHAIN, MATCH_SWAPCHAIN, 1, false,
// 		TEXTURE_USAGE_DEPTH_ATTACHMENT | TEXTURE_USAGE_SAMPLED, NULL);
//
// 	recycler_new_index(&renderer->image_indices);
// 	vulkan_renderer_texture_create(
// 		renderer->context, RENDER_TARGET_TEXTURE_SHADOW_DEPTH,
// 		2048, 2048, 1, false,
// 		TEXTURE_USAGE_DEPTH_ATTACHMENT | TEXTURE_USAGE_SAMPLED, NULL);
//
// 	recycler_new_index(&renderer->image_indices);
// 	vulkan_renderer_texture_create(
// 		renderer->context, RENDER_TARGET_TEXTURE_MAIN_COLOR,
// 		MATCH_SWAPCHAIN, MATCH_SWAPCHAIN, 4, true,
// 		TEXTURE_USAGE_COLOR_ATTACHMENT, NULL);
//
// 	RenderPassDesc shadow = {
// 		.name = str_lit("Shadow"),
// 		.depth_attachment = { .texture = RENDER_TARGET_TEXTURE_SHADOW_DEPTH, .clear = { .depth = 1.0f }, .load = CLEAR, .store = STORE },
// 		.use_depth = true
// 	};
//
// 	RenderPassDesc main = {
// 		.name = str_lit("Main"),
// 		.color_attachments = { { .clear = { .color = GLM_VEC4_BLACK_INIT }, .load = CLEAR, .store = STORE, .present = true } },
// 		.color_attachment_count = 1,
// 		.depth_attachment = { .texture = RENDER_TARGET_TEXTURE_MAIN_DEPTH, .clear = { .depth = 1.0f }, .load = CLEAR, .store = DONT_CARE },
// 		.use_depth = true
// 	};
//
// 	vulkan_renderer_resource_global_create(renderer->context, GLOBAL_VIEW,
// 		(ResourceBinding[]){
// 		  { .binding = 0, .type = SHADER_BINDING_UNIFORM_BUFFER, .size = sizeof(FrameData), .count = 1 },
// 		  { .binding = 1, .type = SHADER_BINDING_TEXTURE_2D, .size = 0, .count = 1 },
// 		  { .binding = 1, .type = SHADER_BINDING_SAMPLER, .size = 0, .count = 1 }, // Same binding == COMBINED_IMAGE_SAMPLER
// 		},
// 		3);
// 	vulkan_renderer_resource_global_set_texture_sampler(renderer->context, GLOBAL_VIEW, 1, RENDER_TARGET_TEXTURE_SHADOW_DEPTH, RENDERER_DEFAULT_SAMPLER_LINEAR);
//
// 	vulkan_renderer_pass_create(renderer->context, RENDERER_DEFAULT_PASS_SHADOW, SHADOW_VIEW, &shadow);
// 	vulkan_renderer_pass_create(renderer->context, RENDERER_DEFAULT_PASS_MAIN, GLOBAL_VIEW, &main);
//
// 	ArenaTemp scratch = arena_scratch(NULL);
// 	{
// 		FileContent vsc = filesystem_read(scratch.arena, str_lit("./assets/shaders/pbr.vert.spv"));
// 		FileContent fsc = filesystem_read(scratch.arena, str_lit("./assets/shaders/pbr.frag.spv"));
//
// 		MaterialParameters default_material_parameters = (MaterialParameters){
// 			.base_color_factor = { 0.8f, 0.8f, 0.8f, 1.0f },
// 			.emissive_factor = { 0.0f, 0.0f, 0.0f, 0.0f },
// 			.roughness_factor = 0.5f,
// 			.metallic_factor = 0.0f,
// 		};
// 		ShaderConfig shader_config = {
// 			.vertex_code = vsc.content,
// 			.vertex_code_size = vsc.size,
// 			.fragment_code = fsc.content,
// 			.fragment_code_size = fsc.size,
// 			.default_ubo_data = &default_material_parameters,
// 			.ubo_size = sizeof(default_material_parameters)
// 		};
//
// 		renderer->default_shader = renderer_shader_create(identifier_generate(), &shader_config);
// 	}
// 	arena_release_scratch(scratch);
//
// 	ShaderParameter parameters[] = {
// 		(ShaderParameter){ .name = str_lit("u_base_color_texture"), .size = sizeof(RTexture), .type = SHADER_PARAMETER_TYPE_TEXTURE, .as.texture.index = RENDERER_DEFAULT_TEXTURE_WHITE },
// 		(ShaderParameter){ .name = str_lit("u_metallic_roughness_texture"), .size = sizeof(RTexture), .type = SHADER_PARAMETER_TYPE_TEXTURE, .as.texture.index = RENDERER_DEFAULT_TEXTURE_WHITE },
// 		(ShaderParameter){ .name = str_lit("u_normal_texture"), .size = sizeof(RTexture), .type = SHADER_PARAMETER_TYPE_TEXTURE, .as.texture.index = RENDERER_DEFAULT_TEXTURE_NORMAL },
// 		(ShaderParameter){ .name = str_lit("u_occlusion_texture"), .size = sizeof(RTexture), .type = SHADER_PARAMETER_TYPE_TEXTURE, .as.texture.index = RENDERER_DEFAULT_TEXTURE_WHITE },
// 		(ShaderParameter){ .name = str_lit("u_emissive_texture"), .size = sizeof(RTexture), .type = SHADER_PARAMETER_TYPE_TEXTURE, .as.texture.index = RENDERER_DEFAULT_TEXTURE_BLACK },
// 	};
// 	renderer->default_material = renderer_material_create(renderer->default_shader, countof(parameters), parameters);
//
// 	return true;
// }
//
// void renderer_system_shutdown(void) {
// 	vulkan_renderer_destroy(renderer->context);
// }
//
// Handle renderer_texture_create(UUID id, TextureConfig *config) {
// 	if (config && config->pixels)
// 		return handle_create_with_uuid(resolve_texture(id, config), id);
// 	LOG_WARN("Invalid texture configuration passed to renderer_create_texture, ignoring request");
// 	return INVALID_HANDLE;
// }
//
// RShader renderer_shader_create(UUID id, ShaderConfig *config) {
// 	if (config)
// 		return handle_create_with_uuid(resolve_shader(id, config), id);
// 	return INVALID_HANDLE;
// }
//
// RShader renderer_shader_default(void) {
// 	return renderer->default_shader;
// }
//
// Handle renderer_mesh_create(UUID id, MeshConfig *config) {
// 	if (config)
// 		return handle_create_with_uuid(resolve_mesh(id, config), id);
// 	return INVALID_HANDLE;
// }
//
// Handle renderer_material_create(RShader rshader, uint32_t parameter_count, ShaderParameter *parameters) {
// 	if (handle_is_valid(rshader) == false)
// 		return INVALID_HANDLE;
//
// 	Shader *shader = ((Shader *)renderer->shader_allocator->slots) + rshader.index;
//
// 	Material *instance = pool_alloc(renderer->material_allocator);
// 	uint32_t instance_index = instance - (Material *)renderer->material_allocator->slots;
//
// 	instance->shader = shader->handle;
//
// 	instance->group_resource_id = recycler_new_index(&renderer->group_indices);
// 	vulkan_renderer_resource_group_create(renderer->context, instance->group_resource_id, shader->handle.index, 256);
//
// 	if (shader->default_ubo_data && shader->ubo_size > 0)
// 		vulkan_renderer_resource_group_write(renderer->context, instance->group_resource_id, 0, 0, shader->ubo_size, shader->default_ubo_data, true);
//
// 	for (uint32_t binding_index = 0; binding_index < shader->reflection.binding_count; ++binding_index) {
// 		ShaderBinding *binding = &shader->reflection.bindings[binding_index];
//
// 		if (binding->frequency == SHADER_UNIFORM_FREQUENCY_PER_MATERIAL && binding->type == SHADER_BINDING_TEXTURE_2D) {
// 			RTexture texture_handle = INVALID_HANDLE;
// 			for (uint32_t parameter_index = 0; parameter_index < parameter_count; ++parameter_index) {
// 				ShaderParameter *parameter = &parameters[parameter_index];
// 				if (string_equals(binding->name, parameter->name))
// 					texture_handle = parameter->as.texture;
// 			}
//
// 			uint32_t handle = RENDERER_DEFAULT_TEXTURE_WHITE;
// 			if (handle_is_valid(texture_handle))
// 				handle = (((Texture *)renderer->texture_allocator->slots) + texture_handle.index)->handle;
//
// 			vulkan_renderer_resource_group_set_texture_sampler(renderer->context, instance->group_resource_id, binding->binding, handle, RENDERER_DEFAULT_SAMPLER_NEAREST);
// 		}
// 	}
//
// 	return (Handle){ .index = instance_index, .id = identifier_generate() };
// }
//
// RMaterial renderer_material_default(void) {
// 	return renderer->default_material;
// }
//
// bool renderer_material_destroy(Handle instance) { return false; }
//
// static bool renderer_material_instance_set(Handle material_handle, uint32_t instance, String name, void *value) {
// 	if (handle_is_valid(material_handle) == false)
// 		return false;
//
// 	Material *material = ((Material *)renderer->material_allocator->slots) + material_handle.index;
// 	Shader *shader = ((Shader *)renderer->shader_allocator->slots) + material->shader.index;
//
// 	ShaderMember *member = shader_reflection_find_member(&shader->reflection, name, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);
// 	if (member == NULL)
// 		return false;
//
// 	vulkan_renderer_resource_group_write(renderer->context, material->group_resource_id, instance, member->offset, member->size, value, false);
// 	return true;
// }
//
// bool renderer_material_setf(RMaterial material, String name, float value) {
// 	return renderer_material_instance_set(material, 0, name, &value);
// }
//
// bool renderer_material_set2fv(RMaterial instance, String name, vec2 value) {
// 	return renderer_material_instance_set(instance, 0, name, value);
// }
// bool renderer_material_set3fv(RMaterial instance, String name, vec3 value) {
// 	return renderer_material_instance_set(instance, 0, name, value);
// }
// bool renderer_material_set4fv(RMaterial instance, String name, vec4 value) {
// 	return renderer_material_instance_set(instance, 0, name, value);
// }
//
// bool renderer_material_set_texture(Handle instance_handle, String name, UUID texture_id) {
// 	if (handle_is_valid(instance_handle) == false)
// 		return false;
//
// 	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->texture_map, texture_id, ResourceEntry);
// 	if (entry == NULL || entry->pool_index == INVALID_INDEX) {
// 		LOG_WARN("Texture passed to renderer_material_instance_set_texture is not uploaded, ignoring request");
// 		return false;
// 	}
//
// 	Texture *texture = ((Texture *)renderer->texture_allocator->slots) + entry->pool_index;
// 	Material *instance = ((Material *)renderer->material_allocator->slots) + instance_handle.index;
// 	Shader *shader = ((Shader *)renderer->shader_allocator->slots) + instance->shader.index;
//
// 	int32_t texture_binding = -1;
// 	for (uint32_t i = 0; i < shader->reflection.binding_count; ++i) {
// 		ShaderBinding *binding = &shader->reflection.bindings[i];
// 		if (string_equals(binding->name, name) && binding->type == SHADER_BINDING_TEXTURE_2D &&
// 			binding->frequency == SHADER_UNIFORM_FREQUENCY_PER_MATERIAL) {
// 			texture_binding = binding->binding;
// 			break;
// 		}
// 	}
//
// 	if (texture_binding == -1) {
// 		LOG_WARN("Non-existant texture binding passed to renderer_material_instance_set_texture, ignoring request");
// 		return false;
// 	}
//
// 	vulkan_renderer_resource_group_set_texture_sampler(renderer->context, instance->group_resource_id, texture_binding, texture->handle, RENDERER_DEFAULT_SAMPLER_NEAREST);
// 	return true;
// }
//
// bool renderer_material_instance_setf(RMaterial material, uint32_t material_instance, String name, float value) {
// 	return renderer_material_instance_set(material, material_instance, name, &value);
// }
//
// bool renderer_material_instance_set2fv(RMaterial instance, uint32_t material_instance, String name, vec2 value) {
// 	return renderer_material_instance_set(instance, material_instance, name, value);
// }
// bool renderer_material_instance_set3fv(RMaterial instance, uint32_t material_instance, String name, vec3 value) {
// 	return renderer_material_instance_set(instance, material_instance, name, value);
// }
// bool renderer_material_instance_set4fv(RMaterial instance, uint32_t material_instance, String name, vec4 value) {
// 	return renderer_material_instance_set(instance, material_instance, name, value);
// }
//
// bool renderer_unload_mesh(UUID id) {
// 	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->mesh_map, id, ResourceEntry);
//
// 	if (entry && entry->pool_index != INVALID_INDEX) {
// 		Mesh *gpu_mesh = ((Mesh *)renderer->mesh_allocator->slots) + entry->pool_index;
//
// 		vulkan_renderer_buffer_destroy(renderer->context, gpu_mesh->vertex_buffer);
// 		recycler_free_index(&renderer->buffer_indices, gpu_mesh->vertex_buffer);
//
// 		if (gpu_mesh->index_count) {
// 			vulkan_renderer_buffer_destroy(renderer->context, gpu_mesh->index_buffer);
// 			recycler_free_index(&renderer->buffer_indices, gpu_mesh->index_buffer);
// 		}
//
// 		pool_free(renderer->mesh_allocator, ((Mesh *)renderer->mesh_allocator->slots) + entry->pool_index);
// 		entry->pool_index = INVALID_INDEX;
// 		return true;
// 	}
//
// 	return false;
// }
//
// bool renderer_frame_begin(Camera *camera, uint32_t light_count, Light *lights) {
// 	if (vulkan_renderer_frame_begin(renderer->context, renderer->display->physical_width, renderer->display->logical_height) == false)
// 		return false;
//     vulkan_renderer_pass_begin(renderer->context, RENDERER_DEFAULT_PASS_MAIN);
//
// 	if (camera) {
// 		FrameData data = { 0 };
//
// 		glm_mat4_identity(data.view);
// 		glm_lookat(camera->position, camera->target, camera->up, data.view);
//
// 		glm_mat4_identity(data.projection);
// 		glm_perspective(glm_rad(camera->fov), (float)renderer->width / (float)renderer->height, 0.1f, 1000.f, data.projection);
// 		data.projection[1][1] *= -1;
//
// 		uint32_t point_light_count = 0;
// 		for (uint32_t light_index = 0; light_index < light_count; ++light_index) {
// 			if (lights[light_index].type == LIGHT_TYPE_DIRECTIONAL)
// 				memcpy(&data.directional_light, lights + light_index, sizeof(Light));
// 			else
// 				memcpy(data.lights + point_light_count++, lights + light_index, sizeof(Light));
// 		}
// 		glm_vec3_dup(camera->position, data.camera_position);
// 		data.light_count = point_light_count;
//
// 		ASSERT(point_light_count != light_count);
//
// 		vulkan_renderer_resource_global_write(renderer->context, GLOBAL_VIEW, 0, sizeof(FrameData), &data);
// 		vulkan_renderer_resource_global_bind(renderer->context, GLOBAL_VIEW);
// 	}
//
// 	return true;
// }
//
// bool renderer_frame_end(void) {
// 	vulkan_renderer_pass_end(renderer->context);
// 	return Vulkan_renderer_frame_end(renderer->context);
// }
//
// // bool renderer_pass_begin(RenderPass pass) {
// // 	return vulkan_renderer_pass_begin(renderer->context, pass);
// // }
// // bool renderer_pass_end(void) {
// //     return vulkan_renderer_pass_end(renderer->context);
// // }
//
// bool renderer_draw_mesh(RMesh mesh_handle, RMaterial material, uint32_t material_instance, mat4 transform) {
// 	if (handle_is_valid(mesh_handle) == false || handle_is_valid(material) == false)
// 		return false;
//
// 	Mesh *mesh = ((Mesh *)renderer->mesh_allocator->slots) + mesh_handle.index;
// 	Material *instance = ((Material *)renderer->material_allocator->slots) + material.index; // This doesn't draw
//
// 	for (uint32_t i = 0; i < 16; i++) {
// 		float val = ((float *)transform)[i];
// 		ASSERT(isnan(val) == false || isinf(val) == false);
// 	}
//
// 	vulkan_renderer_shader_bind(renderer->context, instance->shader.index);
// 	vulkan_renderer_resource_group_bind(renderer->context, instance->group_resource_id, material_instance);
//
// 	vulkan_renderer_resource_local_write(renderer->context, 0, sizeof(mat4), transform);
// 	vulkan_renderer_buffer_bind(renderer->context, mesh->vertex_buffer, 0);
// 	if (mesh->index_count) {
// 		vulkan_renderer_buffer_bind(renderer->context, mesh->index_buffer, mesh->index_size);
// 		// LOG_INFO("Drawing %d indices (%d vertices)", mesh->index_count, mesh->vertex_count);
// 		vulkan_renderer_draw_indexed(renderer->context, mesh->index_count);
//
// 	} else
// 		vulkan_renderer_draw(renderer->context, mesh->vertex_count);
//
// 	return true;
// }
//
// void renderer_state_global_wireframe_set(bool active) {
// 	vulkan_renderer_shader_global_state_wireframe_set(renderer->context, active);
// }
//
// bool renderer_on_resize(uint32_t width, uint32_t height) {
// 	if (vulkan_renderer_on_resize(renderer->context, width, height)) {
// 		renderer->width = width, renderer->height = height;
// 		return true;
// 	}
// 	return false;
// }
//
// uint32_t resolve_mesh(UUID id, MeshConfig *config) {
// 	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->mesh_map, id, ResourceEntry);
// 	if (entry && entry->pool_index != INVALID_INDEX) {
// 		return entry->pool_index;
// 	}
//
// 	entry = hash_trie_insert_hash(renderer->arena, &renderer->mesh_map, id, ResourceEntry);
// 	entry->pool_index = INVALID_INDEX;
//
// 	Mesh *gpu_mesh = pool_alloc(renderer->mesh_allocator);
//
// 	uint32_t pool_index = gpu_mesh - (Mesh *)renderer->mesh_allocator->slots;
// 	entry->pool_index = pool_index;
//
// 	gpu_mesh->vertex_buffer = recycler_new_index(&renderer->buffer_indices);
// 	gpu_mesh->vertex_count = config->vertex_count;
// 	vulkan_renderer_buffer_create(renderer->context, gpu_mesh->vertex_buffer, BUFFER_TYPE_VERTEX, config->vertex_size * config->vertex_count, config->vertices);
//
// 	if (config->index_count != 0) {
// 		gpu_mesh->index_buffer = recycler_new_index(&renderer->buffer_indices);
// 		gpu_mesh->index_size = config->index_size;
// 		gpu_mesh->index_count = config->index_count;
//
// 		vulkan_renderer_buffer_create(renderer->context, gpu_mesh->index_buffer, BUFFER_TYPE_INDEX, config->index_size * config->index_count, config->indices);
// 	}
//
// 	// gpu_mesh->material = renderer->default_material;
//
// 	return pool_index;
// }
//
// uint32_t resolve_texture(UUID id, TextureConfig *config) {
// 	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->texture_map, id, ResourceEntry);
// 	if (entry && entry->pool_index != INVALID_INDEX) {
// 		return entry->pool_index;
// 	}
//
// 	entry = hash_trie_insert_hash(renderer->arena, &renderer->texture_map, id, ResourceEntry);
// 	entry->pool_index = INVALID_INDEX;
//
// 	Texture *texture = pool_alloc(renderer->texture_allocator);
// 	texture->handle = recycler_new_index(&renderer->image_indices);
//
// 	uint32_t pool_index = texture - (Texture *)renderer->texture_allocator->slots;
// 	entry->pool_index = pool_index;
//
// 	vulkan_renderer_texture_create(renderer->context, texture->handle, config->width, config->height, config->channels, config->is_srgb, TEXTURE_USAGE_SAMPLED, config->pixels);
// 	return pool_index;
// }
//
// uint32_t resolve_shader(UUID id, ShaderConfig *config) {
// 	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->material_map, id, ResourceEntry);
// 	if (entry && entry->pool_index != INVALID_INDEX) {
// 		return entry->pool_index;
// 	}
//
// 	entry = hash_trie_insert_hash(renderer->arena, &renderer->material_map, id, ResourceEntry);
// 	Shader *shader = pool_alloc(renderer->shader_allocator);
// 	entry->pool_index = shader - (Shader *)renderer->shader_allocator->slots;
//
// 	shader->handle = handle_create(recycler_new_index(&renderer->shader_indices));
//
// 	PipelineDesc desc = DEFAULT_PIPELINE();
// 	desc.cull_mode = CULL_MODE_BACK;
// 	vulkan_renderer_shader_create(
// 		renderer->arena, renderer->context,
// 		shader->handle.index, GLOBAL_VIEW, config,
// 		desc,
// 		&shader->reflection);
//
// 	shader->ubo_size = 0;
// 	shader->default_ubo_data = NULL;
//
// 	for (uint32_t binding_index = 0; binding_index < shader->reflection.binding_count; ++binding_index) {
// 		ShaderBinding *binding = &shader->reflection.bindings[binding_index];
// 		if (binding->frequency != SHADER_UNIFORM_FREQUENCY_PER_MATERIAL)
// 			continue;
//
// 		if (binding->type == SHADER_BINDING_UNIFORM_BUFFER) {
// 			shader->ubo_size = binding->buffer_layout->size;
// 			shader->default_ubo_data = arena_push(renderer->arena, shader->ubo_size, 16, true);
// 			if (config->default_ubo_data)
// 				memcpy(shader->default_ubo_data, config->default_ubo_data, shader->ubo_size);
// 		}
// 	}
//
// 	return entry->pool_index;
// }
//
// ShaderMember *shader_reflection_find_member(ShaderReflection *reflection, String name, ShaderUniformFrequency frequency) {
// 	for (uint32_t index = 0; index < reflection->binding_count; ++index) {
// 		ShaderBinding *binding = &reflection->bindings[index];
// 		if (binding->frequency != frequency || (binding->type == SHADER_BINDING_UNIFORM_BUFFER || binding->type == SHADER_BINDING_STORAGE_BUFFER) == false)
// 			continue;
//
// 		for (uint32_t member_index = 0; member_index < binding->buffer_layout->member_count; ++member_index) {
// 			if (string_equals(binding->buffer_layout->members[member_index].name, name)) {
// 				return &binding->buffer_layout->members[member_index];
// 			}
// 		}
// 	}
// 	return NULL;
// }
//
// uint32_t shader_reflection_find_binding(ShaderReflection *reflection, String name, ShaderUniformFrequency frequency) {
// 	for (uint32_t index = 0; index < reflection->binding_count; ++index) {
// 		ShaderBinding *binding = &reflection->bindings[index];
// 		if (binding->frequency != frequency)
// 			continue;
//
// 		for (uint32_t member_index = 0; member_index < binding->buffer_layout->member_count; ++member_index) {
// 			if (string_equals(binding->buffer_layout->members[member_index].name, name)) {
// 				return binding->binding;
// 			}
// 		}
// 	}
//
// 	return INVALID_INDEX;
// }
