#pragma once

#include "core/identifiers.h"
#include "renderer/renderer_types.h"

struct arena;

SceneAsset *importer_load_gltf(struct arena *arena, const char *path);

// GLTFPrimitive *importer_load_gltf(struct arena *arena, const char *path);

// Image importer_load_image(const char *path);
// void importer_unload_image(Image image);
