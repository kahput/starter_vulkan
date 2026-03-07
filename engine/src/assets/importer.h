#pragma once

#include "core/astring.h"
#include "core/identifiers.h"

#include "asset_types.h"
#include "core/r_types.h"

struct arena;

ShaderConfig importer_load_shader(Arena *arena, String vertex_path, String fragment_path);
ImageSource importer_load_image(Arena *arena, String path);
bool importer_load_gltf(Arena *arena, String path, SModel *out_model);
