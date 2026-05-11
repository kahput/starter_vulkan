#pragma once

#include "assets/mesh_source.h"
#include "core/strings.h"
#include "core/identifiers.h"

#include "asset_types.h"
#include "core/r_types.h"

struct arena;

/* ENGINE_API Font importer_load_font_ex(Arena *arena, String path, float font_size, uint32_t codepoint_count, const int32_t *codepoints); */ // Doesn't work for arbitrary codepoints yet
ENGINE_API Font importer_load_font(Arena *arena, String path, float font_size);
ENGINE_API ShaderSource importer_load_shader(Arena *arena, String vertex_path, String fragment_path);
ENGINE_API ImageSource importer_load_image(Arena *arena, String path);
ENGINE_API SceneSource importer_load_gltf_scene(Arena *arena, String path);
