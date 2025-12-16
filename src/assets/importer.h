#pragma once

#include "core/astring.h"
#include "core/identifiers.h"

#include "asset_types.h"

struct arena;

TextureSource *importer_load_image(struct arena *arena, String path);
ModelSource *importer_load_gltf(struct arena *arena, String path);
