#pragma once

#include "core/astring.h"
#include "core/identifiers.h"

#include "asset_types.h"

struct arena;

bool importer_load_shader(Arena *arena, String vertex_path, String fragment_path, ShaderSource *out_shader);
bool importer_load_image(Arena *arena, String path, ImageSource *out_texture);
bool importer_load_gltf(Arena *arena, String path, SModel *out_model);
