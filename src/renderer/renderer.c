#include "renderer.h"

#include "allocators/arena.h"
#include "allocators/pool.h"
#include "common.h"
#include "core/hash_trie.h"
#include "core/identifiers.h"
#include "core/logger.h"
#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

#include <assert.h>
#include <stdalign.h>

typedef struct {
	HashTrieNode node;

	uint32_t pool_index;
} ResourceEntry;

typedef struct handle_allocator {
	uint32_t *free_indices;
	uint32_t free_count;
	uint32_t next_unused;
	uint32_t capacity;
} ResourceHandler;

typedef struct model {
	uint32_t mesh_pool_indices[32];
	uint32_t mesh_count;

	uint32_t image_pool_indices[32];
	uint32_t image_count;

	uint32_t material_pool_indices[16];
	uint32_t material_count;
} Model;

void resource_handler_create(Arena *arena, ResourceHandler *handler, uint32_t capacity);

uint32_t resource_handle_new(ResourceHandler *handler);
void resource_handle_free(ResourceHandler *handler, uint32_t handle);

static uint32_t resolve_texture(UUID id, TextureSource *source);
static uint32_t resolve_material(UUID id, MaterialSource *source);
static uint32_t resolve_mesh(UUID id, MeshSource *source);

static struct renderer {
	Arena *arena;
	VulkanContext *context;
	void *display;

	uint32_t width, height;

	uint32_t frame_uniform_buffer;
	Material model_material, sprite_material;

	uint32_t linear_sampler, nearest_sampler;
	uint32_t default_texture_white;
	uint32_t default_texture_black;
	uint32_t default_texture_normal;
	uint32_t default_texture_offset;

	MaterialParameters default_material_parameters;
	MaterialInstance default_material_instance;

	ResourceEntry *model_map;

	ResourceEntry *mesh_map;
	ResourceEntry *texture_map;
	ResourceEntry *material_map;

	Pool *model_allocator;

	Pool *mesh_allocator;
	Pool *texture_allocator;
	Pool *material_allocator;

	ResourceHandler buffer_handler;
	ResourceHandler image_handler;
	ResourceHandler sampler_handler;
	ResourceHandler set_handler;
} *renderer = { 0 };

static bool create_default_material_instance(void);

bool renderer_create(void *memory, size_t offset, size_t size, void *display, uint32_t width, uint32_t height) {
	size_t minimum_footprint = sizeof(Renderer) + sizeof(Arena);

	uintptr_t base_addr = (uintptr_t)memory + offset;
	size_t alignment_needed = alignof(Renderer);
	size_t padding = (alignment_needed - (base_addr % alignment_needed)) % alignment_needed;

	offset += padding;

	if (size < minimum_footprint + padding) {
		LOG_ERROR("Renderer: Failed to create renderer, memory footprint too small");
		assert(false);
		return false;
	}
	renderer = (Renderer *)((uint8_t *)memory + offset);
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
		assert(false);
		return false;
	}

	resource_handler_create(renderer->arena, &renderer->buffer_handler, MAX_BUFFERS);
	resource_handler_create(renderer->arena, &renderer->image_handler, MAX_TEXTURES);
	resource_handler_create(renderer->arena, &renderer->sampler_handler, MAX_SAMPLERS);
	resource_handler_create(renderer->arena, &renderer->set_handler, MAX_RESOURCE_SETS);

	renderer->model_allocator = allocator_pool_from_arena(renderer->arena, 256, sizeof(Model));
	renderer->mesh_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(Mesh));
	renderer->texture_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(Texture));
	renderer->material_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(MaterialInstance));

	// ========================= DEFAULT ======================================
	renderer->frame_uniform_buffer = resource_handle_new(&renderer->buffer_handler);
	vulkan_renderer_create_buffer(renderer->context, renderer->frame_uniform_buffer, BUFFER_TYPE_UNIFORM, sizeof(SceneData), NULL);

	renderer->model_material = (Material){ .shader = 0, .pipeline = 0 };
	vulkan_renderer_create_shader(renderer->context, renderer->model_material.shader, SLITERAL("./assets/shaders/vs_terrain.spv"), SLITERAL("./assets/shaders/fs_terrain.spv"));
	PipelineDesc model_pipeline = DEFAULT_PIPELINE(renderer->model_material.shader);
	vulkan_renderer_create_pipeline(renderer->context, renderer->model_material.pipeline, model_pipeline);

	renderer->sprite_material = (Material){ .shader = 1, .pipeline = 1 };
	vulkan_renderer_create_shader(renderer->context, renderer->sprite_material.shader, SLITERAL("./assets/shaders/vs_sprite.spv"), SLITERAL("./assets/shaders/fs_sprite.spv"));
	PipelineDesc sprite_pipeline = DEFAULT_PIPELINE(renderer->sprite_material.shader);
	sprite_pipeline.cull_mode = CULL_MODE_NONE;
	vulkan_renderer_create_pipeline(renderer->context, renderer->sprite_material.pipeline, sprite_pipeline);

	renderer->default_texture_white = resource_handle_new(&renderer->image_handler);
	uint8_t WHITE[4] = { 255, 255, 255, 255 };
	vulkan_renderer_create_texture(renderer->context, renderer->default_texture_white, 1, 1, 4, WHITE);

	renderer->default_texture_black = resource_handle_new(&renderer->image_handler);
	uint8_t BLACK[4] = { 0, 0, 0, 255 };
	vulkan_renderer_create_texture(renderer->context, renderer->default_texture_black, 1, 1, 4, BLACK);

	renderer->default_texture_normal = resource_handle_new(&renderer->image_handler);
	uint8_t FLAT_NORMAL[4] = { 128, 128, 255, 255 };
	vulkan_renderer_create_texture(renderer->context, renderer->default_texture_normal, 1, 1, 4, FLAT_NORMAL);

	renderer->default_texture_offset = resource_handle_new(&renderer->image_handler);
	renderer->linear_sampler = resource_handle_new(&renderer->sampler_handler);
	vulkan_renderer_create_sampler(renderer->context, renderer->linear_sampler, LINEAR_SAMPLER);

	renderer->nearest_sampler = resource_handle_new(&renderer->sampler_handler);
	vulkan_renderer_create_sampler(renderer->context, renderer->nearest_sampler, NEAREST_SAMPLER);

	renderer->model_material.global_set = resource_handle_new(&renderer->set_handler);
	vulkan_renderer_create_resource_set(renderer->context, renderer->model_material.global_set, renderer->model_material.shader, SHADER_UNIFORM_FREQUENCY_PER_FRAME);
	vulkan_renderer_update_resource_set_buffer(renderer->context, renderer->model_material.global_set, "u_scene", renderer->frame_uniform_buffer);

	renderer->sprite_material.global_set = resource_handle_new(&renderer->set_handler);
	vulkan_renderer_create_resource_set(renderer->context, renderer->sprite_material.global_set, renderer->sprite_material.shader, SHADER_UNIFORM_FREQUENCY_PER_FRAME);
	vulkan_renderer_update_resource_set_buffer(renderer->context, renderer->sprite_material.global_set, "u_scene", renderer->frame_uniform_buffer);

	create_default_material_instance();

	return true;
}

void renderer_destroy(void) {
	vulkan_renderer_destroy(renderer->context);
}

bool renderer_upload_mesh(UUID id, MeshSource *mesh) {
	return resolve_mesh(id, mesh) == INVALID_INDEX ? false : true;
}

bool renderer_unload_mesh(UUID id) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->mesh_map, id, ResourceEntry);

	if (entry && entry->pool_index != INVALID_INDEX) {
		Mesh *gpu_mesh = ((Mesh *)renderer->mesh_allocator->slots) + entry->pool_index;

		vulkan_renderer_destroy_buffer(renderer->context, gpu_mesh->vertex_buffer);
		resource_handle_free(&renderer->buffer_handler, gpu_mesh->vertex_buffer);

		if (gpu_mesh->index_count) {
			vulkan_renderer_destroy_buffer(renderer->context, gpu_mesh->index_buffer);
			resource_handle_free(&renderer->buffer_handler, gpu_mesh->index_buffer);
		}

		pool_free(renderer->mesh_allocator, ((Mesh *)renderer->mesh_allocator->slots) + entry->pool_index);
		entry->pool_index = INVALID_INDEX;
		return true;
	}

	return false;
}

bool renderer_upload_image(UUID id, TextureSource *image) {
	return resolve_texture(id, image) == INVALID_INDEX ? false : true;
}

bool renderer_unload_image(UUID id) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->texture_map, id, ResourceEntry);

	if (entry && entry->pool_index != INVALID_INDEX) {
		Texture *texture = ((Texture *)renderer->texture_allocator->slots) + entry->pool_index;
		vulkan_renderer_destroy_texture(renderer->context, texture->handle);
		resource_handle_free(&renderer->image_handler, texture->handle);

		pool_free(renderer->texture_allocator, ((Texture *)renderer->texture_allocator->slots) + entry->pool_index);
		entry->pool_index = INVALID_INDEX;
		return true;
	}

	return false;
}

bool renderer_upload_material(UUID id, MaterialSource *material) {
	return resolve_material(id, material) == INVALID_INDEX ? false : true;
}

bool renderer_unload_material(UUID id) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->material_map, id, ResourceEntry);

	if (entry && entry->pool_index != INVALID_INDEX) {
		MaterialInstance *gpu_material = ((MaterialInstance *)renderer->material_allocator->slots) + entry->pool_index;

		vulkan_renderer_destroy_resource_set(renderer->context, gpu_material->resource_set);
		resource_handle_free(&renderer->set_handler, gpu_material->resource_set);

		vulkan_renderer_destroy_buffer(renderer->context, gpu_material->parameter_uniform_buffer);
		resource_handle_free(&renderer->buffer_handler, gpu_material->parameter_uniform_buffer);

		pool_free(renderer->material_allocator, ((MaterialInstance *)renderer->material_allocator->slots) + entry->pool_index);
		entry->pool_index = INVALID_INDEX;
		return true;
	}

	return false;
}

bool renderer_upload_model(UUID id, ModelSource *model) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->model_map, id, ResourceEntry);
	if (entry && entry->pool_index != INVALID_INDEX) {
		return true;
	}

	entry = hash_trie_insert_hash(renderer->arena, &renderer->model_map, id, ResourceEntry);
	entry->pool_index = INVALID_INDEX;

	Model *gpu_model = pool_alloc(renderer->model_allocator);
	*gpu_model = (Model){ 0 };

	uint32_t pool_index = gpu_model - (Model *)renderer->model_allocator->slots;
	entry->pool_index = pool_index;

	for (uint32_t image_index = 0; image_index < model->image_count; ++image_index) {
		if (image_index > countof(gpu_model->image_pool_indices)) {
			LOG_ERROR("Renderer: Model texture count exceeds limit");
			assert(false);
		}

		TextureSource *image = &model->images[image_index];
		gpu_model->image_pool_indices[gpu_model->image_count++] = resolve_texture(image->asset_id, image);
	}

	for (uint32_t material_index = 0; material_index < model->material_count; ++material_index) {
		if (material_index > countof(gpu_model->material_pool_indices)) {
			LOG_ERROR("Renderer: Model material count exceeds limit");
			assert(false);
		}

		MaterialSource *material = &model->materials[material_index];
		gpu_model->material_pool_indices[gpu_model->material_count++] = resolve_material(material->asset_id, material);
	}

	for (uint32_t mesh_index = 0; mesh_index < model->mesh_count; ++mesh_index) {
		if (mesh_index > countof(gpu_model->mesh_pool_indices)) {
			LOG_ERROR("Renderer: Model mesh count exceeds limit");
			assert(false);
		}

		MeshSource *mesh = &model->meshes[mesh_index];
		gpu_model->mesh_pool_indices[gpu_model->mesh_count++] = resolve_mesh(mesh->asset_id, mesh);
	}

	return true;
}

bool renderer_begin_frame(Camera *camera) {
	if (vulkan_renderer_begin_frame(renderer->context, renderer->display) == false)
		return false;

	vulkan_renderer_bind_pipeline(renderer->context, renderer->model_material.pipeline);
	vulkan_renderer_bind_resource_set(renderer->context, renderer->model_material.global_set);

	if (camera) {
	SceneData data = { 0 };
	glm_mat4_identity(data.view);
	glm_lookat(camera->position, camera->target, camera->up, data.view);

	glm_mat4_identity(data.projection);
	glm_perspective(glm_rad(camera->fov), (float)renderer->width / (float)renderer->height, 0.1f, 1000.f, data.projection);
	data.projection[1][1] *= -1;

	vulkan_renderer_update_buffer(renderer->context, renderer->frame_uniform_buffer, 0, sizeof(SceneData), &data);

	}

	return true;
}

bool renderer_draw_model(UUID id, mat4 transform) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->model_map, id, ResourceEntry);
	if (entry == NULL || entry->pool_index == INVALID_INDEX) {
		return false;
	}

	Model *model = ((Model *)renderer->model_allocator->slots) + entry->pool_index;
	vulkan_renderer_push_constants(renderer->context, 0, "push_constants", transform);

	for (uint32_t mesh_index = 0; mesh_index < model->mesh_count; ++mesh_index) {
		uint32_t mesh_pool_index = model->mesh_pool_indices[mesh_index];
		Mesh *mesh = ((Mesh *)renderer->mesh_allocator->slots) + mesh_pool_index;
		MaterialInstance *material = mesh->material;

		vulkan_renderer_bind_resource_set(renderer->context, material->resource_set);

		vulkan_renderer_bind_buffer(renderer->context, mesh->vertex_buffer);
		vulkan_renderer_bind_buffer(renderer->context, mesh->index_buffer);

		vulkan_renderer_draw_indexed(renderer->context, mesh->index_count);
	}

	return true;
}

bool renderer_end_frame(void) {
	return Vulkan_renderer_end_frame(renderer->context);
}

bool renderer_resize(uint32_t width, uint32_t height) {
	if (vulkan_renderer_resize(renderer->context, width, height)) {
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
		.material = renderer->model_material,
		.parameter_uniform_buffer = resource_handle_new(&renderer->buffer_handler),
		.resource_set = resource_handle_new(&renderer->set_handler)
	};
	vulkan_renderer_create_buffer(renderer->context, renderer->default_material_instance.parameter_uniform_buffer, BUFFER_TYPE_UNIFORM, sizeof(MaterialParameters), &renderer->default_material_parameters);

	vulkan_renderer_create_resource_set(renderer->context, renderer->default_material_instance.resource_set, renderer->default_material_instance.material.shader, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);
	vulkan_renderer_update_resource_set_buffer(renderer->context, renderer->default_material_instance.resource_set, "u_material", renderer->default_material_instance.parameter_uniform_buffer);

	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, renderer->default_material_instance.resource_set, "u_base_color_texture", renderer->default_texture_white, renderer->linear_sampler);
	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, renderer->default_material_instance.resource_set, "u_metallic_roughness_texture", renderer->default_texture_white, renderer->linear_sampler);
	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, renderer->default_material_instance.resource_set, "u_normal_texture", renderer->default_texture_normal, renderer->linear_sampler);
	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, renderer->default_material_instance.resource_set, "u_occlusion_texture", renderer->default_texture_white, renderer->linear_sampler);
	vulkan_renderer_update_resource_set_texture_sampler(renderer->context, renderer->default_material_instance.resource_set, "u_emissive_texture", renderer->default_texture_black, renderer->linear_sampler);

	return true;
}

void resource_handler_create(Arena *arena, ResourceHandler *handler, uint32_t capacity) {
	handler->free_indices = arena_push_array_zero(arena, uint32_t, capacity);
	handler->free_count = handler->next_unused = 0;
	handler->capacity = capacity;
}

uint32_t resource_handle_new(ResourceHandler *handler) {
	if (handler->free_count > 0)
		return handler->free_indices[--handler->free_count];

	assert(handler->next_unused < handler->capacity);
	return handler->next_unused++;
}
void resource_handle_free(ResourceHandler *handler, uint32_t handle) {
	handler->free_indices[handler->free_count++] = handle;
}

uint32_t resolve_texture(UUID id, TextureSource *source) {
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
	texture->handle = resource_handle_new(&renderer->image_handler);

	uint32_t pool_index = texture - (Texture *)renderer->texture_allocator->slots;
	entry->pool_index = pool_index;

	vulkan_renderer_create_texture(renderer->context, texture->handle, source->width, source->height, source->channels, source->pixels);

	return pool_index;
}

uint32_t resolve_material(UUID id, MaterialSource *source) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->material_map, id, ResourceEntry);
	if (entry && entry->pool_index != INVALID_INDEX) {
		return entry->pool_index;
	}

	entry = hash_trie_insert_hash(renderer->arena, &renderer->material_map, id, ResourceEntry);
	entry->pool_index = INVALID_INDEX;

	MaterialInstance *gpu_material = pool_alloc(renderer->material_allocator);
	gpu_material->parameter_uniform_buffer = resource_handle_new(&renderer->buffer_handler);
	gpu_material->resource_set = resource_handle_new(&renderer->set_handler);

	uint32_t pool_index = gpu_material - (MaterialInstance *)renderer->material_allocator->slots;
	entry->pool_index = pool_index;

	vulkan_renderer_create_resource_set(renderer->context, gpu_material->resource_set, renderer->model_material.shader, SHADER_UNIFORM_FREQUENCY_PER_MATERIAL);

	MaterialParameters parameters = {
		.base_color_factor = {
		  source->base_color_factor[0],
		  source->base_color_factor[1],
		  source->base_color_factor[2],
		  source->base_color_factor[3],
		},
		.metallic_factor = source->metallic_factor,
		.roughness_factor = source->roughness_factor,
		.emissive_factor = {
		  source->emissive_factor[0],
		  source->emissive_factor[1],
		  source->emissive_factor[2],
		}
	};

	vulkan_renderer_create_buffer(renderer->context, gpu_material->parameter_uniform_buffer, BUFFER_TYPE_UNIFORM, sizeof(MaterialParameters), &parameters);
	vulkan_renderer_update_resource_set_buffer(renderer->context, gpu_material->resource_set, "u_material", gpu_material->parameter_uniform_buffer);

	// TODO: Use source->base_color_texture->asset_id;

	if (source->base_color_texture) {
		uint32_t pool_index = resolve_texture(source->base_color_texture->asset_id, source->base_color_texture);
		Texture *texture = ((Texture *)renderer->texture_allocator->slots) + pool_index;
		vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_base_color_texture", texture->handle, renderer->linear_sampler);
	} else
		vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_base_color_texture", renderer->default_texture_white, renderer->linear_sampler);

	if (source->metallic_roughness_texture) {
		uint32_t pool_index = resolve_texture(source->metallic_roughness_texture->asset_id, source->metallic_roughness_texture);
		Texture *texture = ((Texture *)renderer->texture_allocator->slots) + pool_index;
		vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_metallic_roughness_texture", texture->handle, renderer->linear_sampler);
	} else
		vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_metallic_roughness_texture", renderer->default_texture_white, renderer->linear_sampler);

	if (source->normal_texture) {
		uint32_t pool_index = resolve_texture(source->normal_texture->asset_id, source->normal_texture);
		Texture *texture = ((Texture *)renderer->texture_allocator->slots) + pool_index;
		vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_normal_texture", texture->handle, renderer->linear_sampler);
	} else
		vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_normal_texture", renderer->default_texture_normal, renderer->linear_sampler);

	if (source->occlusion_texture) {
		uint32_t pool_index = resolve_texture(source->occlusion_texture->asset_id, source->occlusion_texture);
		Texture *texture = ((Texture *)renderer->texture_allocator->slots) + pool_index;
		vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_occlusion_texture", texture->handle, renderer->linear_sampler);
	} else
		vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_occlusion_texture", renderer->default_texture_white, renderer->linear_sampler);

	if (source->emissive_texture) {
		uint32_t pool_index = resolve_texture(source->emissive_texture->asset_id, source->emissive_texture);
		Texture *texture = ((Texture *)renderer->texture_allocator->slots) + pool_index;
		vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_emissive_texture", texture->handle, renderer->linear_sampler);
	} else
		vulkan_renderer_update_resource_set_texture_sampler(renderer->context, gpu_material->resource_set, "u_emissive_texture", renderer->default_texture_black, renderer->linear_sampler);

	return pool_index;
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

	gpu_mesh->vertex_buffer = resource_handle_new(&renderer->buffer_handler);
	gpu_mesh->vertex_count = source->vertex_count;
	vulkan_renderer_create_buffer(renderer->context, gpu_mesh->vertex_buffer, BUFFER_TYPE_VERTEX, sizeof(*source->vertices) * gpu_mesh->vertex_count, source->vertices);

	uint32_t material_index = resolve_material(source->material->asset_id, source->material);
	gpu_mesh->material = ((MaterialInstance *)renderer->material_allocator->slots) + material_index;

	if (source->index_count) {
		gpu_mesh->index_buffer = resource_handle_new(&renderer->buffer_handler);
		gpu_mesh->index_count = source->index_count;

		vulkan_renderer_create_buffer(renderer->context, gpu_mesh->index_buffer, BUFFER_TYPE_INDEX, sizeof(*source->indices) * source->index_count, source->indices);
	}

	return pool_index;
}
