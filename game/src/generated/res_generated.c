#include "res_generated.h"
#include "res.h"

RES_ImageMeta res_image_metadata[RES_IMAGE_MAX] = {
	[RES_IMAGE_BASE_GRASS] = {
	  .name = comp8("base_grass"),
	  .filepath = comp8("assets/images/base_grass.png"),
	},
	[RES_IMAGE_BLENDING_TRANSPARENT_WINDOW] = {
	  .name = comp8("blending_transparent_window"),
	  .filepath = comp8("assets/images/blending_transparent_window.png"),
	},
	[RES_IMAGE_GRASS] = {
	  .name = comp8("grass"),
	  .filepath = comp8("assets/images/grass.png"),
	},
	[RES_IMAGE_HEART] = {
	  .name = comp8("heart"),
	  .filepath = comp8("assets/images/heart.png"),
	},
	[RES_IMAGE_HEIGHTMAP] = {
	  .name = comp8("heightmap"),
	  .filepath = comp8("assets/images/heightmap.png"),
	},
};

RES_FontFaceMeta res_pixeloid_sans_faces[] = {
	[0] = {
	  .filepath = comp8("assets/fonts/PixeloidSans.ttf"),
	  .style = RES_FONT_STYLE_NORMAL,
	  .min_weight = RES_FONT_WEIGHT_REGULAR,
	  .max_weight = RES_FONT_WEIGHT_REGULAR,
	},
};

RES_FontFaceMeta res_ibm_plex_mono_faces[] = {
	[0] = {
	  .filepath = comp8("/usr/share/fonts/TTF/IBMPlexMono-Light.ttf"),
	  .style = RES_FONT_STYLE_NORMAL,
	  .min_weight = RES_FONT_WEIGHT_LIGHT,
	  .max_weight = RES_FONT_WEIGHT_LIGHT,
	},
	[1] = {
	  .filepath = comp8("/usr/share/fonts/TTF/IBMPlexMono-Regular.ttf"),
	  .style = RES_FONT_STYLE_NORMAL,
	  .min_weight = RES_FONT_WEIGHT_REGULAR,
	  .max_weight = RES_FONT_WEIGHT_REGULAR,
	},
	[2] = {
	  .filepath = comp8("/usr/share/fonts/TTF/IBMPlexMono-Bold.ttf"),
	  .style = RES_FONT_STYLE_NORMAL,
	  .min_weight = RES_FONT_WEIGHT_BOLD,
	  .max_weight = RES_FONT_WEIGHT_BOLD,
	},
	[3] = {
	  .filepath = comp8("/usr/share/fonts/TTF/IBMPlexMono-LightItalic.ttf"),
	  .style = RES_FONT_STYLE_ITALIC,
	  .min_weight = RES_FONT_WEIGHT_LIGHT,
	  .max_weight = RES_FONT_WEIGHT_LIGHT,
	},
	[4] = {
	  .filepath = comp8("/usr/share/fonts/TTF/IBMPlexMono-Italic.ttf"),
	  .style = RES_FONT_STYLE_ITALIC,
	  .min_weight = RES_FONT_WEIGHT_REGULAR,
	  .max_weight = RES_FONT_WEIGHT_REGULAR,
	},
	[5] = {
	  .filepath = comp8("/usr/share/fonts/TTF/IBMPlexMono-BoldItalic.ttf"),
	  .style = RES_FONT_STYLE_ITALIC,
	  .min_weight = RES_FONT_WEIGHT_BOLD,
	  .max_weight = RES_FONT_WEIGHT_BOLD,
	},
};

RES_FontFamilyMeta res_font_metadata[RES_FONT_MAX] = {
	[RES_FONT_PIXELOID_SANS] = {
	  .name = comp8("Pixeloid Sans"),
	  .faces = res_pixeloid_sans_faces,
	  .face_count = countof(res_pixeloid_sans_faces),
	},
	[RES_FONT_IBM_PLEX_MONO] = {
	  .name = comp8("IBM Plex Mono"),
	  .faces = res_ibm_plex_mono_faces,
	  .face_count = countof(res_ibm_plex_mono_faces),
	},
};
