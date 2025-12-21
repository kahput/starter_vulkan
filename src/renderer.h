#pragma once

#include "assets/asset_types.h"
#include "core/identifiers.h"
#include "renderer/renderer_types.h"

bool renderer_system_startup(void *memory, size_t size, void *display, uint32_t width, uint32_t height);
void renderer_system_shutdown(void);

Handle renderer_upload_mesh(UUID id, MeshSource *mesh);

bool renderer_begin_frame(Camera *camera);
bool renderer_draw_mesh(Handle handle, mat4 transform);
bool renderer_end_frame(void);

bool renderer_resize(uint32_t width, uint32_t height);
