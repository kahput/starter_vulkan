#include "renderer.h"

#include "allocators/arena.h"
#include "allocators/pool.h"

#include "common.h"
#include "core/debug.h"
#include "core/logger.h"
#include "core/hash_trie.h"
#include "core/identifiers.h"

#include "renderer/renderer_types.h"
#include "renderer/vk_renderer.h"

#include <assert.h>
#include <stdalign.h>

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
	Material model_material, sprite_material;

	uint32_t default_texture_white;
	uint32_t default_texture_black;
	uint32_t default_texture_normal;
	uint32_t default_texture_offset;

	uint32_t linear_sampler, nearest_sampler;

	MaterialParameters default_material_parameters;
	MaterialInstance default_material_instance;

	ResourceEntry *mesh_map;
	Pool *mesh_allocator;

	IndexRecycler buffer_indices;
	IndexRecycler image_indices;
	IndexRecycler sampler_indices;
	IndexRecycler set_indices;
} Renderer;

static Renderer *renderer = { 0 };

static bool create_default_material_instance(void);
static uint32_t resolve_mesh(UUID id, MeshSource *source);

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

	index_recycler_create(renderer->arena, &renderer->buffer_indices, MAX_BUFFERS);
	index_recycler_create(renderer->arena, &renderer->image_indices, MAX_TEXTURES);
	index_recycler_create(renderer->arena, &renderer->sampler_indices, MAX_SAMPLERS);
	index_recycler_create(renderer->arena, &renderer->set_indices, MAX_RESOURCE_SETS);

	renderer->mesh_allocator = allocator_pool_from_arena(renderer->arena, 1024, sizeof(Mesh));

	// ========================= DEFAULT ======================================
	renderer->frame_uniform_buffer = recycler_new_index(&renderer->buffer_indices);
	vulkan_renderer_create_buffer(renderer->context, renderer->frame_uniform_buffer, BUFFER_TYPE_UNIFORM, sizeof(FrameData), NULL);

	renderer->model_material = (Material){ .shader = 0 };
	vulkan_renderer_create_shader(renderer->context, renderer->model_material.shader, S("./assets/shaders/pbr.vert.spv"), S("./assets/shaders/pbr.frag.spv"), DEFAULT_PIPELINE());

	renderer->sprite_material = (Material){ .shader = 1 };
	PipelineDesc sprite_pipeline = DEFAULT_PIPELINE();
	sprite_pipeline.cull_mode = CULL_MODE_NONE;
	vulkan_renderer_create_shader(renderer->context, renderer->sprite_material.shader, S("./assets/shaders/sprite.vert.spv"), S("./assets/shaders/sprite.frag.spv"), sprite_pipeline);

	renderer->default_texture_white = recycler_new_index(&renderer->image_indices);
	uint8_t WHITE[4] = { 255, 255, 255, 255 };
	vulkan_renderer_create_texture(renderer->context, renderer->default_texture_white, 1, 1, 4, WHITE);

	renderer->default_texture_black = recycler_new_index(&renderer->image_indices);
	uint8_t BLACK[4] = { 0, 0, 0, 255 };
	vulkan_renderer_create_texture(renderer->context, renderer->default_texture_black, 1, 1, 4, BLACK);

	renderer->default_texture_normal = recycler_new_index(&renderer->image_indices);
	uint8_t FLAT_NORMAL[4] = { 128, 128, 255, 255 };
	vulkan_renderer_create_texture(renderer->context, renderer->default_texture_normal, 1, 1, 4, FLAT_NORMAL);

	renderer->default_texture_offset = recycler_new_index(&renderer->image_indices);
	renderer->linear_sampler = recycler_new_index(&renderer->sampler_indices);
	vulkan_renderer_create_sampler(renderer->context, renderer->linear_sampler, LINEAR_SAMPLER);

	renderer->nearest_sampler = recycler_new_index(&renderer->sampler_indices);
	vulkan_renderer_create_sampler(renderer->context, renderer->nearest_sampler, NEAREST_SAMPLER);

	vulkan_renderer_set_global_resource_buffer(renderer->context, renderer->frame_uniform_buffer);
	create_default_material_instance();

	return true;
}

void renderer_system_shutdown(void) {
	vulkan_renderer_destroy(renderer->context);
}

Handle renderer_upload_mesh(UUID id, MeshSource *mesh) {
	return (Handle){ .id = id, .index = resolve_mesh(id, mesh) };
}

bool renderer_unload_mesh(UUID id) {
	ResourceEntry *entry = hash_trie_lookup_hash(&renderer->mesh_map, id, ResourceEntry);

	if (entry && entry->pool_index != INVALID_INDEX) {
		Mesh *gpu_mesh = ((Mesh *)renderer->mesh_allocator->slots) + entry->pool_index;

		vulkan_renderer_destroy_buffer(renderer->context, gpu_mesh->vertex_buffer);
		recycler_free_index(&renderer->buffer_indices, gpu_mesh->vertex_buffer);

		if (gpu_mesh->index_count) {
			vulkan_renderer_destroy_buffer(renderer->context, gpu_mesh->index_buffer);
			recycler_free_index(&renderer->buffer_indices, gpu_mesh->index_buffer);
		}

		pool_free(renderer->mesh_allocator, ((Mesh *)renderer->mesh_allocator->slots) + entry->pool_index);
		entry->pool_index = INVALID_INDEX;
		return true;
	}

	return false;
}

bool renderer_begin_frame(Camera *camera) {
	if (vulkan_renderer_begin_frame(renderer->context, renderer->display) == false)
		return false;

	if (camera) {
		FrameData data = { 0 };
		glm_mat4_identity(data.view);
		glm_lookat(camera->position, camera->target, camera->up, data.view);

		glm_mat4_identity(data.projection);
		glm_perspective(glm_rad(camera->fov), (float)renderer->width / (float)renderer->height, 0.1f, 1000.f, data.projection);
		data.projection[1][1] *= -1;

		vulkan_renderer_update_buffer(renderer->context, renderer->frame_uniform_buffer, 0, sizeof(FrameData), &data);
	}

	return true;
}

bool renderer_draw_mesh(Handle handle, mat4 transform) {
	if (handle_valid(handle) == false)
		return false;

	Mesh *mesh = ((Mesh *)renderer->mesh_allocator->slots) + handle.index;
	MaterialInstance *material = mesh->material;

	vulkan_renderer_bind_shader(renderer->context, material->material.shader, material->resource_set);
	vulkan_renderer_push_constants(renderer->context, 0, S("push_constants"), transform);

	vulkan_renderer_bind_buffer(renderer->context, mesh->vertex_buffer);
	if (mesh->index_count) {
		vulkan_renderer_bind_buffer(renderer->context, mesh->index_buffer);
		vulkan_renderer_draw_indexed(renderer->context, mesh->index_count);

	} else
		vulkan_renderer_draw(renderer->context, mesh->vertex_count);

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
		.material_parameter_buffer = recycler_new_index(&renderer->buffer_indices),
		.resource_set = recycler_new_index(&renderer->set_indices)
	};
	vulkan_renderer_create_buffer(renderer->context, renderer->default_material_instance.material_parameter_buffer, BUFFER_TYPE_UNIFORM, sizeof(MaterialParameters), &renderer->default_material_parameters);

	vulkan_renderer_create_shader_resource(renderer->context, renderer->default_material_instance.resource_set, renderer->default_material_instance.material.shader);
	vulkan_renderer_set_shader_resource_buffer(renderer->context, renderer->model_material.shader, renderer->default_material_instance.resource_set, S("u_material"), renderer->default_material_instance.material_parameter_buffer);

	vulkan_renderer_set_shader_resource_texture_sampler(
		renderer->context,
		renderer->model_material.shader, renderer->default_material_instance.resource_set,
		S("u_base_color_texture"), renderer->default_texture_white, renderer->linear_sampler);

	vulkan_renderer_set_shader_resource_texture_sampler(
		renderer->context,
		renderer->model_material.shader, renderer->default_material_instance.resource_set,
		S("u_metallic_roughness_texture"), renderer->default_texture_white, renderer->linear_sampler);

	vulkan_renderer_set_shader_resource_texture_sampler(
		renderer->context,
		renderer->model_material.shader, renderer->default_material_instance.resource_set,
		S("u_normal_texture"), renderer->default_texture_normal, renderer->linear_sampler);

	vulkan_renderer_set_shader_resource_texture_sampler(
		renderer->context,
		renderer->model_material.shader, renderer->default_material_instance.resource_set,
		S("u_occlusion_texture"), renderer->default_texture_white, renderer->linear_sampler);

	vulkan_renderer_set_shader_resource_texture_sampler(
		renderer->context,
		renderer->model_material.shader, renderer->default_material_instance.resource_set,
		S("u_emissive_texture"), renderer->default_texture_black, renderer->linear_sampler);

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
	vulkan_renderer_create_buffer(renderer->context, gpu_mesh->vertex_buffer, BUFFER_TYPE_VERTEX, sizeof(*source->vertices) * gpu_mesh->vertex_count, source->vertices);

	gpu_mesh->material = &renderer->default_material_instance;

	if (source->index_count) {
		gpu_mesh->index_buffer = recycler_new_index(&renderer->buffer_indices);
		gpu_mesh->index_count = source->index_count;

		vulkan_renderer_create_buffer(renderer->context, gpu_mesh->index_buffer, BUFFER_TYPE_INDEX, sizeof(*source->indices) * source->index_count, source->indices);
	}

	return pool_index;
}
