#include "renderer.h"

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
#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

#include <assert.h>
#include <cglm/vec3.h>
#include <stdalign.h>
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
	MaterialBase default_PBR;

	uint32_t default_texture_white;
	uint32_t default_texture_black;
	uint32_t default_texture_normal;
	uint32_t default_texture_offset;

	uint32_t linear_sampler, nearest_sampler;

	MaterialParameters default_material_parameters;
	MaterialInstance default_material_instance;

	ResourceEntry *texture_map;
	Pool *texture_allocator;

	ResourceEntry *material_map;
	Pool *material_allocator;
	Pool *instance_allocator;

	ResourceEntry *mesh_map;
	Pool *mesh_allocator;

	IndexRecycler shader_indices;
	IndexRecycler buffer_indices;
	IndexRecycler image_indices;
	IndexRecycler sampler_indices;
	IndexRecycler set_indices;
} Renderer;

static Renderer *renderer = { 0 };

static bool create_default_material_instance(void);
static ShaderMember *find_member_in_reflection(ShaderReflection *reflection, String name, ShaderUniformFrequency frequency);

static uint32_t resolve_mesh(UUID id, MeshSource *source);
// static uint32_t resolve_image(UUID id, ImageSource *source);
static uint32_t resolve_material(UUID id, MaterialSource *source);

bool renderer_system_startup(void *memory, size_t size, void *display, uint32_t width, uint32_t height) {
	size_t minimum_footprint = sizeof(Renderer) + sizeof(Arena);

	uintptr_t base_addr = (uintptr_t)memory;
	size_t alignment_needed = alignof(Renderer);
	size_t padding = (alignment_needed - (base_addr % alignment_needed)) % alignment_needed;

	if (size < minimum_footprint + padding) {
		LOG_ERROR("Renderer: Failed to create renderer, memory footprint too small");
		ASSERT(false);
		return false;
	}
	renderer = (Renderer *)((uint8_t *)memory + padding);
	renderer->arena = (Arena *)(renderer + 1);
	renderer->display = display;

	renderer->width = width, renderer->height = height;

	*renderer->arena = (Arena){
		.memory = renderer->arena + 1,
		.offset = 0,
		.capacity = size - minimum_footprint
	};

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
	renderer->material_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(MaterialBase), alignof(MaterialBase));
	renderer->instance_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(MaterialInstance), alignof(MaterialInstance));

	// ========================= DEFAULT ======================================
	renderer->frame_uniform_buffer = recycler_new_index(&renderer->buffer_indices);
	vulkan_renderer_buffer_create(renderer->context, renderer->frame_uniform_buffer, BUFFER_TYPE_UNIFORM, sizeof(FrameData), NULL);
	vulkan_renderer_global_resource_set_buffer(renderer->context, renderer->frame_uniform_buffer);

	renderer->default_PBR = (MaterialBase){ 0 };
	renderer->default_PBR.shader = handle_create(recycler_new_index(&renderer->shader_indices));

	ArenaTemp scratch = arena_scratch(NULL);
	{
		FileContent vsc = filesystem_read(scratch.arena, S("./assets/shaders/pbr.vert.spv"));
		FileContent fsc = filesystem_read(scratch.arena, S("./assets/shaders/pbr.frag.spv"));
		vulkan_renderer_shader_create(renderer->arena, renderer->context, renderer->default_PBR.shader.index, vsc, fsc, DEFAULT_PIPELINE(), &renderer->default_PBR.reflection);
	}
	arena_release_scratch(scratch);

	renderer->default_texture_white = recycler_new_index(&renderer->image_indices);
	uint8_t WHITE[4] = { 255, 255, 255, 255 };
	vulkan_renderer_texture_create(renderer->context, renderer->default_texture_white, 1, 1, 4, WHITE);

	renderer->default_texture_black = recycler_new_index(&renderer->image_indices);
	uint8_t BLACK[4] = { 0, 0, 0, 255 };
	vulkan_renderer_texture_create(renderer->context, renderer->default_texture_black, 1, 1, 4, BLACK);

	renderer->default_texture_normal = recycler_new_index(&renderer->image_indices);
	uint8_t FLAT_NORMAL[4] = { 128, 128, 255, 255 };
	vulkan_renderer_texture_create(renderer->context, renderer->default_texture_normal, 1, 1, 4, FLAT_NORMAL);
	renderer->default_texture_offset = renderer->default_texture_normal;

	renderer->linear_sampler = recycler_new_index(&renderer->sampler_indices);
	vulkan_renderer_sampler_create(renderer->context, renderer->linear_sampler, LINEAR_SAMPLER);

	renderer->nearest_sampler = recycler_new_index(&renderer->sampler_indices);
	vulkan_renderer_sampler_create(renderer->context, renderer->nearest_sampler, NEAREST_SAMPLER);

	create_default_material_instance();
	return true;
}

void renderer_system_shutdown(void) {
	vulkan_renderer_destroy(renderer->context);
}

Handle renderer_upload_material_base(UUID id, MaterialSource *source) {
	if (source)
		return (Handle){ .id = id, .index = resolve_material(id, source) };
	return INVALID_HANDLE;
}

Handle renderer_upload_mesh(UUID id, MeshSource *mesh) {
	return (Handle){ .id = id, .index = resolve_mesh(id, mesh) };
}

Handle renderer_material_instance_create(Handle base_material_handle) {
	if (handle_valid(base_material_handle) == false)
		return INVALID_HANDLE;
	MaterialBase *base = ((MaterialBase *)renderer->material_allocator->slots) + base_material_handle.index;

	MaterialInstance *instance = pool_alloc(renderer->instance_allocator);
	uint32_t instance_index = instance - (MaterialInstance *)renderer->instance_allocator->slots;

	instance->base = *base;

	// TODO: Do lazy override instead
	instance->override_resource_id = base->instance_count++;
	vulkan_renderer_shader_resource_create(renderer->context, instance->override_resource_id, base->shader.index);

	if (base->ubo_size > 0) {
		instance->override_ubo_id = recycler_new_index(&renderer->buffer_indices);

		vulkan_renderer_buffer_create(
			renderer->context,
			instance->override_ubo_id,
			BUFFER_TYPE_UNIFORM,
			base->ubo_size,
			base->default_ubo_data);

		// TODO: Store ubo_binding_index in MaterialBase to avoid this loop
		uint32_t ubo_binding = 0;
		for (uint32_t i = 0; i < base->reflection.binding_count; ++i) {
			if (base->reflection.bindings[i].type == SHADER_BINDING_UNIFORM_BUFFER &&
				base->reflection.bindings[i].frequency == SHADER_UNIFORM_FREQUENCY_PER_MATERIAL) {
				ubo_binding = base->reflection.bindings[i].binding;
				break;
			}
		}

		vulkan_renderer_shader_resource_set_buffer(
			renderer->context,
			base->shader.index,
			instance->override_resource_id,
			ubo_binding,
			instance->override_ubo_id);
	}

	for (uint32_t i = 0; i < base->reflection.binding_count; ++i) {
		ShaderBinding *b = &base->reflection.bindings[i];
		if (b->frequency == SHADER_UNIFORM_FREQUENCY_PER_MATERIAL && b->type == SHADER_BINDING_COMBINED_IMAGE_SAMPLER) {
			uint32_t texture_pool_index = base->default_textures[b->binding];
			Texture *texture = ((Texture *)renderer->texture_allocator->slots) + texture_pool_index;

			vulkan_renderer_shader_resource_set_texture_sampler(
				renderer->context,
				base->shader.index,
				instance->override_resource_id,
				b->binding,
				texture->handle,
				renderer->nearest_sampler // Default sampler, maybe allow override later
			);
		}
	}

	return (Handle){ .index = instance_index, .id = identifier_generate() };
}

bool renderer_material_instance_destroy(Handle instance) { return false; }

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

bool renderer_draw_mesh(Handle mesh_handle, Handle material_instance_handle, mat4 transform) {
	if (handle_valid(mesh_handle) == false || handle_valid(material_instance_handle) == false)
		return false;

	Mesh *mesh = ((Mesh *)renderer->mesh_allocator->slots) + mesh_handle.index;
	// MaterialInstance *instance = mesh->material;
	MaterialInstance *instance = ((MaterialInstance *)renderer->instance_allocator->slots) + material_instance_handle.index; // This doesn't draw

	vulkan_renderer_shader_bind(renderer->context, instance->base.shader.index, 0);
	vulkan_renderer_push_constants(renderer->context, 0, 0, sizeof(mat4), transform);

	vulkan_renderer_buffer_bind(renderer->context, mesh->vertex_buffer);
	if (mesh->index_count) {
		vulkan_renderer_buffer_bind(renderer->context, mesh->index_buffer);
		vulkan_renderer_draw_indexed(renderer->context, mesh->index_count);

	} else
		vulkan_renderer_draw(renderer->context, mesh->vertex_count);

	return true;
}

bool renderer_end_frame(void) {
	return Vulkan_renderer_frame_end(renderer->context);
}

bool renderer_resize(uint32_t width, uint32_t height) {
	if (vulkan_renderer_on_resize(renderer->context, width, height)) {
		renderer->width = width, renderer->height = height;
		return true;
	}
	return false;
}

bool create_default_material_instance(void) {
	renderer->default_material_parameters = (MaterialParameters){
		.base_color_factor = { 0.8f, 0.8f, 0.8f, 1.0f },
		.emissive_factor = { 0.0f, 0.0f, 0.0f, 0.0f },
		.roughness_factor = 0.5f,
		.metallic_factor = 0.0f,
	};

	renderer->default_material_instance = (MaterialInstance){
		.base = renderer->default_PBR,
		.override_resource_id = renderer->default_PBR.instance_count++,
		.override_ubo_id = recycler_new_index(&renderer->buffer_indices),
	};
	vulkan_renderer_buffer_create(renderer->context, renderer->default_material_instance.override_ubo_id, BUFFER_TYPE_UNIFORM, sizeof(MaterialParameters), &renderer->default_material_parameters);

	vulkan_renderer_shader_resource_create(renderer->context, renderer->default_material_instance.override_resource_id, renderer->default_material_instance.base.shader.index);
	vulkan_renderer_shader_resource_set_buffer(renderer->context, renderer->default_PBR.shader.index, renderer->default_material_instance.override_resource_id, 5, renderer->default_material_instance.override_ubo_id);

	vulkan_renderer_shader_resource_set_texture_sampler(
		renderer->context,
		renderer->default_PBR.shader.index, renderer->default_material_instance.override_resource_id,
		0, renderer->default_texture_white, renderer->linear_sampler);

	vulkan_renderer_shader_resource_set_texture_sampler(
		renderer->context,
		renderer->default_PBR.shader.index, renderer->default_material_instance.override_resource_id,
		1, renderer->default_texture_white, renderer->linear_sampler);

	vulkan_renderer_shader_resource_set_texture_sampler(
		renderer->context,
		renderer->default_PBR.shader.index, renderer->default_material_instance.override_resource_id,
		2, renderer->default_texture_normal, renderer->linear_sampler);

	vulkan_renderer_shader_resource_set_texture_sampler(
		renderer->context,
		renderer->default_PBR.shader.index, renderer->default_material_instance.override_resource_id,
		3, renderer->default_texture_white, renderer->linear_sampler);

	vulkan_renderer_shader_resource_set_texture_sampler(
		renderer->context,
		renderer->default_PBR.shader.index, renderer->default_material_instance.override_resource_id,
		4, renderer->default_texture_black, renderer->linear_sampler);

	return true;
}

uint32_t resolve_mesh(UUID id, MeshSource *source) {
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
	gpu_mesh->vertex_count = source->vertex_count;
	vulkan_renderer_buffer_create(renderer->context, gpu_mesh->vertex_buffer, BUFFER_TYPE_VERTEX, sizeof(*source->vertices) * gpu_mesh->vertex_count, source->vertices);

	gpu_mesh->material = &renderer->default_material_instance;

	if (source->index_count) {
		gpu_mesh->index_buffer = recycler_new_index(&renderer->buffer_indices);
		gpu_mesh->index_count = source->index_count;

		vulkan_renderer_buffer_create(renderer->context, gpu_mesh->index_buffer, BUFFER_TYPE_INDEX, sizeof(*source->indices) * source->index_count, source->indices);
	}

	return pool_index;
}

uint32_t resolve_texture(UUID id, ImageSource *source) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->texture_map, id, ResourceEntry);
	if (entry && entry->pool_index != INVALID_INDEX) {
		return entry->pool_index;
	}

	if (!source) {
		LOG_WARN("Renderer: Attempted to resolve null texture source for UUID %llu", id);
		return INVALID_INDEX;
	}

	entry = hash_trie_insert_hash(renderer->arena, &renderer->texture_map, id, ResourceEntry);
	entry->pool_index = INVALID_INDEX; // Only difference, doesn't matter

	Texture *texture = pool_alloc(renderer->texture_allocator);
	texture->handle = recycler_new_index(&renderer->image_indices);

	uint32_t pool_index = texture - (Texture *)renderer->texture_allocator->slots;
	entry->pool_index = pool_index;

	vulkan_renderer_texture_create(renderer->context, texture->handle, source->width, source->height, source->channels, source->pixels);

	return pool_index;
}

uint32_t resolve_material(UUID id, MaterialSource *source) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->material_map, id, ResourceEntry);
	if (entry && entry->pool_index != INVALID_INDEX) {
		return entry->pool_index;
	}

	entry = hash_trie_insert_hash(renderer->arena, &renderer->material_map, id, ResourceEntry);
	MaterialBase *base = pool_alloc(renderer->material_allocator);
	entry->pool_index = base - (MaterialBase *)renderer->material_allocator->slots;

	base->shader = handle_create_with_uuid(recycler_new_index(&renderer->shader_indices), source->shader->id);
	base->flags = 0;
	base->instance_count = 0;

	vulkan_renderer_shader_create(
		renderer->arena, renderer->context,
		base->shader.index, source->shader->vertex_shader, source->shader->fragment_shader,
		source->description,
		&base->reflection);

	for (uint32_t index = 0; index < countof(base->default_textures); ++index)
		base->default_textures[index] = renderer->default_texture_white;

	base->ubo_size = 0;
	base->default_ubo_data = NULL;

	for (uint32_t binding_index = 0; binding_index < base->reflection.binding_count; ++binding_index) {
		ShaderBinding *binding = &base->reflection.bindings[binding_index];
		if (binding->frequency != SHADER_UNIFORM_FREQUENCY_PER_MATERIAL)
			continue;

		if (binding->type == SHADER_BINDING_UNIFORM_BUFFER) {
			base->ubo_size = binding->buffer_layout->size;
			base->default_ubo_data = arena_push_zero(renderer->arena, base->ubo_size, 16);
		}

		if (binding->type == SHADER_BINDING_COMBINED_IMAGE_SAMPLER)
			if (string_contains(binding->name, S("normal")) != -1)
				base->default_textures[binding->binding] = renderer->default_texture_normal;
	}

	for (uint32_t index = 0; index < source->property_count; ++index) {
		MaterialProperty *property = &source->properties[index];

		if (property->type == PROPERTY_TYPE_IMAGE) {
			for (uint32_t binding_index = 0; binding_index < base->reflection.binding_count; ++binding_index) {
				if (string_equals(base->reflection.bindings[binding_index].name, property->name)) {
					base->default_textures[base->reflection.bindings[binding_index].binding] = resolve_texture(property->as.image->id, property->as.image);
					break;
				}
			}
		} else {
			ShaderMember *member = find_member_in_reflection(&base->reflection, property->name, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);

			if (member && base->default_ubo_data) {
				if (member->offset + member->size > base->ubo_size)
					continue;

				memcpy((uint8_t *)base->default_ubo_data + member->offset, &property->as, member->size);
			}
		}
	}

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
