#include "renderer.h"
#include "renderer/r_internal.h"
#include "renderer/backend/vulkan_api.h"

#include "allocators/arena.h"
#include "allocators/pool.h"

#include "assets/asset_types.h"
#include "common.h"
#include "core/astring.h"
#include "core/debug.h"
#include "core/logger.h"
#include "core/hash_trie.h"
#include "core/identifiers.h"

#include "platform/filesystem.h"

#include <cglm/vec3.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct {
	HashTrieNode node;

	uint32_t pool_index;
} ResourceEntry;

typedef struct model {
	uint32_t mesh_pool_indices[32];
	uint32_t mesh_count;

	uint32_t image_pool_indices[32];
	uint32_t image_count;

	uint32_t material_pool_indices[16];
	uint32_t material_count;
} Model;

typedef struct renderer {
	Arena *arena;
	VulkanContext *context;
	void *display;

	uint32_t width, height;

	uint32_t global_set;

	uint32_t frame_uniform_buffer;

	RShader default_shader;
	RMaterial default_material;

	RTexture default_texture_white;
	RTexture default_texture_black;
	RTexture default_texture_normal;

	uint32_t linear_sampler, nearest_sampler;

	ResourceEntry *texture_map;
	Pool *texture_allocator;

	ResourceEntry *material_map;
	Pool *shader_allocator;
	Pool *material_allocator;

	ResourceEntry *mesh_map;
	Pool *mesh_allocator;

	IndexRecycler shader_indices;
	IndexRecycler buffer_indices;
	IndexRecycler image_indices;
	IndexRecycler sampler_indices;
	IndexRecycler set_indices;
} Renderer;

static Renderer *renderer = { 0 };

static ShaderMember *find_member_in_reflection(ShaderReflection *reflection, String name, ShaderUniformFrequency frequency);

static uint32_t resolve_mesh(UUID id, MeshConfig *config);
static uint32_t resolve_texture(UUID id, TextureConfig *config);
static uint32_t resolve_shader(UUID id, ShaderConfig *config);

bool renderer_system_startup(void *memory, size_t size, void *display, uint32_t width, uint32_t height) {
	size_t minimum_footprint = sizeof(Renderer) + sizeof(Arena);

	uintptr_t base_addr = (uintptr_t)memory;
	uintptr_t aligned_addr = (uintptr_t)aligned_address((uintptr_t)memory, alignof(Arena));
	size_t padding = aligned_addr - base_addr;

	if (size < minimum_footprint + padding) {
		LOG_ERROR("Renderer: Failed to create renderer, memory footprint too small");
		ASSERT(false);
		return false;
	}
	Arena *arena = (Arena *)aligned_addr;
	*arena = (Arena){
		.memory = arena + 1,
		.offset = 0,
		.capacity = size - sizeof(Arena)
	};

	renderer = arena_push_struct_zero(arena, Renderer);
	renderer->arena = arena;

	renderer->display = display;
	renderer->width = width, renderer->height = height;

	if (vulkan_renderer_create(renderer->arena, renderer->display, &renderer->context) == false) {
		LOG_ERROR("Renderer: Failed to create vulkan context");
		ASSERT(false);
		return false;
	}

	index_recycler_create(renderer->arena, &renderer->shader_indices, MAX_SHADERS);
	index_recycler_create(renderer->arena, &renderer->buffer_indices, MAX_BUFFERS);
	index_recycler_create(renderer->arena, &renderer->image_indices, MAX_TEXTURES);
	index_recycler_create(renderer->arena, &renderer->sampler_indices, MAX_SAMPLERS);
	index_recycler_create(renderer->arena, &renderer->set_indices, MAX_RESOURCE_SETS);

	renderer->mesh_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(Mesh), alignof(Mesh));
	renderer->texture_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(Texture), alignof(Texture));
	renderer->shader_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(Shader), alignof(Shader));
	renderer->material_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(Material), alignof(Material));

	// ========================= DEFAULT ======================================
	renderer->frame_uniform_buffer = recycler_new_index(&renderer->buffer_indices);
	vulkan_renderer_buffer_create(renderer->context, renderer->frame_uniform_buffer, BUFFER_TYPE_UNIFORM, sizeof(FrameData), NULL);
	vulkan_renderer_global_resource_set_buffer(renderer->context, renderer->frame_uniform_buffer);

	uint8_t WHITE[4] = { 255, 255, 255, 255 };
	TextureConfig config = { .pixels = WHITE, .width = 1, .height = 1, .channels = 4, .is_srgb = false };
	renderer->default_texture_white = renderer_texture_create(identifier_generate(), &config);

	uint8_t BLACK[4] = { 0, 0, 0, 255 };
	config = (TextureConfig){ .pixels = BLACK, .width = 1, .height = 1, .channels = 4, .is_srgb = false };
	renderer->default_texture_black = renderer_texture_create(identifier_generate(), &config);

	uint8_t FLAT_NORMAL[4] = { 128, 128, 255, 255 };
	config = (TextureConfig){ .pixels = FLAT_NORMAL, .width = 1, .height = 1, .channels = 4, .is_srgb = false };
	renderer->default_texture_normal = renderer_texture_create(identifier_generate(), &config);

	renderer->linear_sampler = recycler_new_index(&renderer->sampler_indices);
	vulkan_renderer_sampler_create(renderer->context, renderer->linear_sampler, LINEAR_SAMPLER);

	renderer->nearest_sampler = recycler_new_index(&renderer->sampler_indices);
	vulkan_renderer_sampler_create(renderer->context, renderer->nearest_sampler, NEAREST_SAMPLER);

	ArenaTemp scratch = arena_scratch(NULL);
	{
		FileContent vsc = filesystem_read(scratch.arena, S("./assets/shaders/pbr.vert.spv"));
		FileContent fsc = filesystem_read(scratch.arena, S("./assets/shaders/pbr.frag.spv"));

		MaterialParameters default_material_parameters = (MaterialParameters){
			.base_color_factor = { 0.8f, 0.8f, 0.8f, 1.0f },
			.emissive_factor = { 0.0f, 0.0f, 0.0f, 0.0f },
			.roughness_factor = 0.5f,
			.metallic_factor = 0.0f,
		};
		ShaderConfig shader_config = {
			.vertex_code = vsc.content,
			.vertex_code_size = vsc.size,
			.fragment_code = fsc.content,
			.fragment_code_size = fsc.size,
			.default_ubo_data = &default_material_parameters,
			.ubo_size = sizeof(default_material_parameters)
		};

		renderer->default_shader = renderer_shader_create(identifier_generate(), &shader_config);
	}
	arena_release_scratch(scratch);

	renderer->default_material = renderer_material_create(renderer->default_shader);

	renderer_material_set_texture(renderer->default_material, S("u_base_color_texture"), renderer->default_texture_white.id);
	renderer_material_set_texture(renderer->default_material, S("u_metallic_roughness_texture"), renderer->default_texture_white.id);
	renderer_material_set_texture(renderer->default_material, S("u_normal_texture"), renderer->default_texture_normal.id);
	renderer_material_set_texture(renderer->default_material, S("u_occlusion_texture"), renderer->default_texture_white.id);
	renderer_material_set_texture(renderer->default_material, S("u_emissive_texture"), renderer->default_texture_black.id);

	return true;
}

void renderer_system_shutdown(void) {
	vulkan_renderer_destroy(renderer->context);
}

Handle renderer_texture_create(UUID id, TextureConfig *config) {
	if (config && config->pixels)
		return handle_create_with_uuid(resolve_texture(id, config), id);
	LOG_WARN("Invalid texture configuration passed to renderer_create_texture, ignoring request");
	return INVALID_HANDLE;
}

RShader renderer_shader_create(UUID id, ShaderConfig *config) {
	if (config)
		return handle_create_with_uuid(resolve_shader(id, config), id);
	return INVALID_HANDLE;
}

RShader renderer_shader_default(void) {
	return renderer->default_shader;
}

Handle renderer_mesh_create(UUID id, MeshConfig *config) {
	if (config)
		return handle_create_with_uuid(resolve_mesh(id, config), id);
	return INVALID_HANDLE;
}

Handle renderer_material_create(Handle base_material_handle) {
	if (handle_valid(base_material_handle) == false)
		return INVALID_HANDLE;
	Shader *shader = ((Shader *)renderer->shader_allocator->slots) + base_material_handle.index;

	Material *instance = pool_alloc(renderer->material_allocator);
	uint32_t instance_index = instance - (Material *)renderer->material_allocator->slots;

	instance->shader = shader->handle;

	// TODO: Do lazy override instead
	instance->override_resource_id = shader->instance_count++;
	vulkan_renderer_shader_resource_create(renderer->context, instance->override_resource_id, shader->handle.index);

	if (shader->ubo_size > 0) {
		instance->override_ubo_id = recycler_new_index(&renderer->buffer_indices);

		vulkan_renderer_buffer_create(
			renderer->context,
			instance->override_ubo_id,
			BUFFER_TYPE_UNIFORM,
			shader->ubo_size,
			shader->default_ubo_data);

		// TODO: Store ubo_binding_index in MaterialBase to avoid this loop
		uint32_t ubo_binding = 0;
		for (uint32_t i = 0; i < shader->reflection.binding_count; ++i) {
			if (shader->reflection.bindings[i].type == SHADER_BINDING_UNIFORM_BUFFER &&
				shader->reflection.bindings[i].frequency == SHADER_UNIFORM_FREQUENCY_PER_MATERIAL) {
				ubo_binding = shader->reflection.bindings[i].binding;
				break;
			}
		}

		vulkan_renderer_shader_resource_set_buffer(
			renderer->context,
			shader->handle.index,
			instance->override_resource_id,
			ubo_binding,
			instance->override_ubo_id);
	}

	for (uint32_t i = 0; i < shader->reflection.binding_count; ++i) {
		ShaderBinding *b = &shader->reflection.bindings[i];
		if (b->frequency == SHADER_UNIFORM_FREQUENCY_PER_MATERIAL && b->type == SHADER_BINDING_TEXTURE_2D) {
			RTexture rtexture = shader->default_textures[b->binding];
			Texture *texture = ((Texture *)renderer->texture_allocator->slots) + rtexture.index;

			vulkan_renderer_shader_resource_set_texture_sampler(
				renderer->context,
				shader->handle.index,
				instance->override_resource_id,
				b->binding,
				texture->handle,
				renderer->nearest_sampler // Default sampler, maybe allow override later
			);
		}
	}

	return (Handle){ .index = instance_index, .id = identifier_generate() };
}

RMaterial renderer_material_default(void) {
	return renderer->default_material;
}

bool renderer_material_destroy(Handle instance) { return false; }

static bool renderer_material_instance_set(Handle instance_handle, String name, void *value) {
	if (handle_valid(instance_handle) == false)
		return false;

	Material *instance = ((Material *)renderer->material_allocator->slots) + instance_handle.index;
	Shader *shader = ((Shader *)renderer->shader_allocator->slots) + instance->shader.index;

	ShaderMember *member = find_member_in_reflection(&shader->reflection, name, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);
	if (member == NULL)
		return false;

	vulkan_renderer_buffer_update(renderer->context, instance->override_ubo_id, member->offset, member->size, value);
	return true;
}

bool renderer_material_setf(Handle instance_handle, String name, float value) {
	return renderer_material_instance_set(instance_handle, name, &value);
}

bool renderer_material_set2fv(Handle instance, String name, vec2 value) {
	return renderer_material_instance_set(instance, name, value);
}
bool renderer_material_set3fv(Handle instance, String name, vec3 value) {
	return renderer_material_instance_set(instance, name, value);
}
bool renderer_material_set4fv(Handle instance, String name, vec4 value) {
	return renderer_material_instance_set(instance, name, value);
}

bool renderer_material_set_texture(Handle instance_handle, String name, UUID texture_id) {
	if (handle_valid(instance_handle) == false)
		return false;

	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->texture_map, texture_id, ResourceEntry);
	if (entry == NULL || entry->pool_index == INVALID_INDEX) {
		LOG_WARN("Texture passed to renderer_material_instance_set_texture is not uploaded, ignoring request");
		return false;
	}

	Texture *texture = ((Texture *)renderer->texture_allocator->slots) + entry->pool_index;
	Material *instance = ((Material *)renderer->material_allocator->slots) + instance_handle.index;
	Shader *shader = ((Shader *)renderer->shader_allocator->slots) + instance->shader.index;

	int32_t texture_binding = -1;
	for (uint32_t i = 0; i < shader->reflection.binding_count; ++i) {
		ShaderBinding *binding = &shader->reflection.bindings[i];
		if (string_equals(binding->name, name) && binding->type == SHADER_BINDING_TEXTURE_2D &&
			binding->frequency == SHADER_UNIFORM_FREQUENCY_PER_MATERIAL) {
			texture_binding = binding->binding;
			break;
		}
	}

	if (texture_binding == -1) {
		LOG_WARN("Non-existant texture binding passed to renderer_material_instance_set_texture, ignoring request");
		return false;
	}

	vulkan_renderer_shader_resource_set_texture_sampler(renderer->context, shader->handle.index, instance->override_resource_id, texture_binding, texture->handle, renderer->nearest_sampler);
	return true;
}

// bool renderer_material_instance_commit(Handle instance_handle) {
// 	return true;
// }

bool renderer_unload_mesh(UUID id) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->mesh_map, id, ResourceEntry);

	if (entry && entry->pool_index != INVALID_INDEX) {
		Mesh *gpu_mesh = ((Mesh *)renderer->mesh_allocator->slots) + entry->pool_index;

		vulkan_renderer_buffer_destroy(renderer->context, gpu_mesh->vertex_buffer);
		recycler_free_index(&renderer->buffer_indices, gpu_mesh->vertex_buffer);

		if (gpu_mesh->index_count) {
			vulkan_renderer_buffer_destroy(renderer->context, gpu_mesh->index_buffer);
			recycler_free_index(&renderer->buffer_indices, gpu_mesh->index_buffer);
		}

		pool_free(renderer->mesh_allocator, ((Mesh *)renderer->mesh_allocator->slots) + entry->pool_index);
		entry->pool_index = INVALID_INDEX;
		return true;
	}

	return false;
}

bool renderer_begin_frame(Camera *camera) {
	if (vulkan_renderer_frame_begin(renderer->context, renderer->display) == false)
		return false;

	if (camera) {
		FrameData data = { 0 };
		glm_mat4_identity(data.view);
		glm_lookat(camera->position, camera->target, camera->up, data.view);

		glm_mat4_identity(data.projection);
		glm_perspective(glm_rad(camera->fov), (float)renderer->width / (float)renderer->height, 0.1f, 1000.f, data.projection);
		data.projection[1][1] *= -1;

		glm_vec3_dup(camera->position, data.camera_position);

		vulkan_renderer_buffer_update(renderer->context, renderer->frame_uniform_buffer, 0, sizeof(FrameData), &data);
	}

	return true;
}

bool renderer_draw_mesh(RMesh mesh_handle, RMaterial material_instance_handle, mat4 transform) {
	if (handle_valid(mesh_handle) == false || handle_valid(material_instance_handle) == false)
		return false;

	Mesh *mesh = ((Mesh *)renderer->mesh_allocator->slots) + mesh_handle.index;
	Material *instance = ((Material *)renderer->material_allocator->slots) + material_instance_handle.index; // This doesn't draw

	for (uint32_t i =0; i < 16; i++) {
		float val = ((float*)transform)[i];
		ASSERT(isnan(val) == false || isinf(val) == false);
	}

	vulkan_renderer_shader_bind(renderer->context, instance->shader.index, instance->override_resource_id);
	vulkan_renderer_push_constants(renderer->context, 0, 0, sizeof(mat4), transform);

	vulkan_renderer_buffer_bind(renderer->context, mesh->vertex_buffer, 0);
	if (mesh->index_count) {
		vulkan_renderer_buffer_bind(renderer->context, mesh->index_buffer, mesh->index_size);
		// LOG_INFO("Drawing %d indices (%d vertices)", mesh->index_count, mesh->vertex_count);
		vulkan_renderer_draw_indexed(renderer->context, mesh->index_count);

	} else
		vulkan_renderer_draw(renderer->context, mesh->vertex_count);

	return true;
}

bool renderer_end_frame(void) {
	return Vulkan_renderer_frame_end(renderer->context);
}

void renderer_state_global_wireframe_set(bool active) {
	vulkan_renderer_shader_global_state_wireframe_set(renderer->context, active);
}

bool renderer_on_resize(uint32_t width, uint32_t height) {
	if (vulkan_renderer_on_resize(renderer->context, width, height)) {
		renderer->width = width, renderer->height = height;
		return true;
	}
	return false;
}

uint32_t resolve_mesh(UUID id, MeshConfig *config) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->mesh_map, id, ResourceEntry);
	if (entry && entry->pool_index != INVALID_INDEX) {
		return entry->pool_index;
	}

	entry = hash_trie_insert_hash(renderer->arena, &renderer->mesh_map, id, ResourceEntry);
	entry->pool_index = INVALID_INDEX;

	Mesh *gpu_mesh = pool_alloc(renderer->mesh_allocator);

	uint32_t pool_index = gpu_mesh - (Mesh *)renderer->mesh_allocator->slots;
	entry->pool_index = pool_index;

	gpu_mesh->vertex_buffer = recycler_new_index(&renderer->buffer_indices);
	gpu_mesh->vertex_count = config->vertex_count;
	vulkan_renderer_buffer_create(renderer->context, gpu_mesh->vertex_buffer, BUFFER_TYPE_VERTEX, config->vertex_size * config->vertex_count, config->vertices);

	if (config->index_count != 0) {
		gpu_mesh->index_buffer = recycler_new_index(&renderer->buffer_indices);
		gpu_mesh->index_size = config->index_size;
		gpu_mesh->index_count = config->index_count;

		vulkan_renderer_buffer_create(renderer->context, gpu_mesh->index_buffer, BUFFER_TYPE_INDEX, config->index_size * config->index_count, config->indices);
	}

	gpu_mesh->material = renderer->default_material;

	return pool_index;
}

uint32_t resolve_texture(UUID id, TextureConfig *config) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->texture_map, id, ResourceEntry);
	if (entry && entry->pool_index != INVALID_INDEX) {
		return entry->pool_index;
	}

	entry = hash_trie_insert_hash(renderer->arena, &renderer->texture_map, id, ResourceEntry);
	entry->pool_index = INVALID_INDEX;

	Texture *texture = pool_alloc(renderer->texture_allocator);
	texture->handle = recycler_new_index(&renderer->image_indices);

	uint32_t pool_index = texture - (Texture *)renderer->texture_allocator->slots;
	entry->pool_index = pool_index;

	vulkan_renderer_texture_create(renderer->context, texture->handle, config->width, config->height, config->channels, config->is_srgb, config->pixels);
	return pool_index;
}

uint32_t resolve_shader(UUID id, ShaderConfig *config) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->material_map, id, ResourceEntry);
	if (entry && entry->pool_index != INVALID_INDEX) {
		return entry->pool_index;
	}

	entry = hash_trie_insert_hash(renderer->arena, &renderer->material_map, id, ResourceEntry);
	Shader *shader = pool_alloc(renderer->shader_allocator);
	entry->pool_index = shader - (Shader *)renderer->shader_allocator->slots;

	shader->handle = handle_create(recycler_new_index(&renderer->shader_indices));
	shader->instance_count = 0;

	PipelineDesc desc = DEFAULT_PIPELINE();
	desc.cull_mode = CULL_MODE_NONE;
	vulkan_renderer_shader_create(
		renderer->arena, renderer->context,
		shader->handle.index, config,
		desc,
		&shader->reflection);

	for (uint32_t index = 0; index < countof(shader->default_textures); ++index)
		shader->default_textures[index] = renderer->default_texture_white;

	shader->ubo_size = 0;
	shader->default_ubo_data = NULL;

	for (uint32_t binding_index = 0; binding_index < shader->reflection.binding_count; ++binding_index) {
		ShaderBinding *binding = &shader->reflection.bindings[binding_index];
		if (binding->frequency != SHADER_UNIFORM_FREQUENCY_PER_MATERIAL)
			continue;

		if (binding->type == SHADER_BINDING_UNIFORM_BUFFER) {
			shader->ubo_size = binding->buffer_layout->size;
			shader->default_ubo_data = arena_push_zero(renderer->arena, shader->ubo_size, 16);
			if (config->default_ubo_data)
				memcpy(shader->default_ubo_data, config->default_ubo_data, shader->ubo_size);
		}

		if (binding->type == SHADER_BINDING_TEXTURE_2D)
			if (string_contains(binding->name, S("normal")) != -1)
				shader->default_textures[binding->binding] = renderer->default_texture_normal;
	}

	// for (uint32_t index = 0; index < config->property_count; ++index) {
	// 	MaterialProperty *property = &config->properties[index];
	//
	// 	if (property->type == PROPERTY_TYPE_IMAGE) {
	// 		for (uint32_t binding_index = 0; binding_index < shader->reflection.binding_count; ++binding_index) {
	// 			if (string_equals(shader->reflection.bindings[binding_index].name, property->name)) {
	// 				shader->default_textures[shader->reflection.bindings[binding_index].binding] = resolve_image(property->as.image->id, property->as.image);
	// 				break;
	// 			}
	// 		}
	// 	} else {
	// 		ShaderMember *member = find_member_in_reflection(&shader->reflection, property->name, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);
	//
	// 		if (member && shader->default_ubo_data) {
	// 			if (member->offset + member->size > shader->ubo_size)
	// 				continue;
	//
	// 			memcpy((uint8_t *)shader->default_ubo_data + member->offset, &property->as, member->size);
	// 		}
	// 	}
	// }

	return entry->pool_index;

	// vulkan_renderer_create_resource_set(renderer->context, gpu_material->resource_set, renderer->model_material.shader, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);
	//
	// MaterialParameters parameters = {
	// 	.base_color_factor = {
	// 	  source->base_color_factor[0],
	// 	  source->base_color_factor[1],
	// 	  source->base_color_factor[2],
	// 	  source->base_color_factor[3],
	// 	},
	// 	.metallic_factor = source->metallic_factor,
	// 	.roughness_factor = source->roughness_factor,
	// 	.emissive_factor = {
	// 	  source->emissive_factor[0],
	// 	  source->emissive_factor[1],
	// 	  source->emissive_factor[2],
	// 	}
	// };
	//
	// vulkan_renderer_create_buffer(renderer->context, gpu_material->parameter_uniform_buffer, BUFFER_TYPE_UNIFORM, sizeof(MaterialParameters), &parameters);
	// vulkan_renderer_update_resource_set_buffer(renderer->context, gpu_material->resource_set, "u_material", gpu_material->parameter_uniform_buffer);
	//
	// if (source->base_color_texture) {
	// 	uint32_t pool_index = resolve_texture(source->base_color_texture->id, source->base_color_texture);
	// 	Texture *texture = ((Texture *)renderer->texture_allocator->slots) + pool_index;
	// 	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_base_color_texture", texture->handle, renderer->linear_sampler);
	// } else
	// 	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_base_color_texture", renderer->default_texture_white, renderer->linear_sampler);
	//
	// if (source->metallic_roughness_texture) {
	// 	uint32_t pool_index = resolve_texture(source->metallic_roughness_texture->id, source->metallic_roughness_texture);
	// 	Texture *texture = ((Texture *)renderer->texture_allocator->slots) + pool_index;
	// 	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_metallic_roughness_texture", texture->handle, renderer->linear_sampler);
	// } else
	// 	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_metallic_roughness_texture", renderer->default_texture_white, renderer->linear_sampler);
	//
	// if (source->normal_texture) {
	// 	uint32_t pool_index = resolve_texture(source->normal_texture->id, source->normal_texture);
	// 	Texture *texture = ((Texture *)renderer->texture_allocator->slots) + pool_index;
	// 	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_normal_texture", texture->handle, renderer->linear_sampler);
	// } else
	// 	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_normal_texture", renderer->default_texture_normal, renderer->linear_sampler);
	//
	// if (source->occlusion_texture) {
	// 	uint32_t pool_index = resolve_texture(source->occlusion_texture->id, source->occlusion_texture);
	// 	Texture *texture = ((Texture *)renderer->texture_allocator->slots) + pool_index;
	// 	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_occlusion_texture", texture->handle, renderer->linear_sampler);
	// } else
	// 	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_occlusion_texture", renderer->default_texture_white, renderer->linear_sampler);
	//
	// if (source->emissive_texture) {
	// 	uint32_t pool_index = resolve_texture(source->emissive_texture->id, source->emissive_texture);
	// 	Texture *texture = ((Texture *)renderer->texture_allocator->slots) + pool_index;
	// 	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_emissive_texture", texture->handle, renderer->linear_sampler);
	// } else
	// 	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_emissive_texture", renderer->default_texture_black, renderer->linear_sampler);
	//
}

ShaderMember *find_member_in_reflection(ShaderReflection *reflection, String name, ShaderUniformFrequency frequency) {
	for (uint32_t index = 0; index < reflection->binding_count; ++index) {
		ShaderBinding *binding = &reflection->bindings[index];
		if (binding->frequency != frequency || (binding->type == SHADER_BINDING_UNIFORM_BUFFER || binding->type == SHADER_BINDING_STORAGE_BUFFER) == false)
			continue;

		for (uint32_t member_index = 0; member_index < binding->buffer_layout->member_count; ++member_index) {
			if (string_equals(binding->buffer_layout->members[member_index].name, name)) {
				return &binding->buffer_layout->members[member_index];
			}
		}
	}
	return NULL;
}
