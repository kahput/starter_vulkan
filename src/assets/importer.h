#pragma once

#include "core/identifiers.h"
#include "renderer/renderer_types.h"

struct arena;
typedef enum {
	ASSET_TYPE_MESH,
	ASSET_TYPE_TEXTURE,
	ASSET_TYPE_AUDIO,
	ASSET_TYPE_FONT,
} AssetType;
typedef Handle HAsset;

typedef struct {
	AssetType type;

	union {
		GLTFPrimitive *gltf_primitive;
	} as;
} Asset;

#define MAX_ASSETS 1024

bool asset_system_startup(struct arena *arena);
bool asset_system_shutdown(void);

HAsset asset_load_model(const char *path);
bool asset_unload_model(HAsset asset);

Asset *asset_get(HAsset asset);

GLTFPrimitive *importer_load_gltf(struct arena *arena, const char *path);

Image importer_load_image(const char *path);
void importer_unload_image(Image image);
