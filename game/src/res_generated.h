#pragma once

#include "core/strings.h"

typedef struct {
    String8 name;
	String8 filepath;
} RES_ImageMetadata;

typedef enum {
	RES_IMAGE_BASE_GRASS,
	RES_IMAGE_BLENDING_TRANSPARENT_WINDOW,
	RES_IMAGE_GRASS,
	RES_IMAGE_HEART,
	RES_IMAGE_HEIGHTMAP,

	RES_IMAGE_MAX,
} RES_ImageID;
extern RES_ImageMetadata res_image_metadata[RES_IMAGE_MAX];
