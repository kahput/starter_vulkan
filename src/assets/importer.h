#pragma once

#include "renderer/renderer_types.h"

struct arena;

Model *importer_load_gltf(struct arena *arena, const char *path);

Image importer_load_image(const char *path);
void importer_unload_image(Image image);
