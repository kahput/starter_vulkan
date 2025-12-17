#pragma once

#include "assets/asset_types.h"
#include "core/identifiers.h"
#include "renderer/renderer_types.h"

typedef struct renderer Renderer;

bool renderer_create(void *memory, size_t offset, size_t size, void *display, uint32_t width, uint32_t height);
void renderer_destroy(void);

bool renderer_upload_mesh(UUID id, MeshSource *mesh);
bool renderer_upload_image(UUID, TextureSource *image);
bool renderer_upload_material(UUID id, MaterialSource *material);

bool renderer_upload_model(UUID id, ModelSource *model);

bool renderer_unload_mesh(UUID id);
bool renderer_unload_image(UUID id);
bool renderer_unload_material(UUID id);

bool renderer_unload_model(UUID id);

bool renderer_begin_frame(Camera *camera);

bool renderer_draw_model(UUID id, mat4 transform);

bool renderer_end_frame(void);

bool renderer_resize(uint32_t width, uint32_t height);
