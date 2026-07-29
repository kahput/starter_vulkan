#pragma once

#include "common.h"
#include "core/geom.h"
#include "core/strings.h"

#include "gfx/font.h"

#define IMGUI_MAX_CHILDREN 32

typedef enum {
	IMGUI_MODE_FIT,
	IMGUI_MODE_FIXED,
	IMGUI_MODE_GROW,

	IMGUI_MODE_MAX,
} IMGUI_SizingMode;

typedef enum {
	IMGUI_DOCK_NONE,

	IMGUI_DOCK_LEFT,
	IMGUI_DOCK_TOP,
	IMGUI_DOCK_CENTER,
	IMGUI_DOCK_RIGHT,
	IMGUI_DOCK_BOTTOM,

	IMGUI_DOCK_MAX,
} IMGUI_Dock;

typedef enum {
	IMGUI_ALIGN_LEFT,
	IMGUI_ALIGN_TOP = IMGUI_ALIGN_LEFT,

	IMGUI_ALIGN_CENTER,

	IMGUI_ALIGN_RIGHT,
	IMGUI_ALIGN_BOTTOM = IMGUI_ALIGN_RIGHT,
} IMGUI_Align;

typedef enum {
	IMGUI_HORIZONTAL,
	IMGUI_VERTICAL,
} IMGUI_Direction;

typedef struct {
	IMGUI_Direction flow;
	IMGUI_Align align[AXIS_MAX2D];
	IMGUI_SizingMode mode[AXIS_MAX2D];
	float child_gap, padding[AXIS_MAX2D][2];
	float border_radius;
	Color bg, fg;

	Font *font;
	String8 text;
	Image2D *image;
} IMGUI_Settings;

typedef struct IMGUI_Widget IMGUI_Widget;
struct IMGUI_Widget {
	uint64_t id;
	IMGUI_Settings settings;

	// Tree
	uint32_t parent;
	uint32_t children[IMGUI_MAX_CHILDREN];
	uint32_t children_count;

	// calculated
	float offset[AXIS_MAX2D];
	float size[AXIS_MAX2D];
};

extern IMGUI_Widget IMGUI_NIL;
bool imgui_valid(IMGUI_Widget *node);

typedef struct {
	Font *font;

	IMGUI_Direction flow;
	IMGUI_SizingMode mode[2];

	IMGUI_Align align[2];

	Color bg, fg;
	float border_radius;

	float p;
	float ph, pv;
	float pt, pr, pb, pl;

	float gap;
} IMGUI_Style;

#define IMGUI_MAX_WIDGETS 256
#define IMGUI_MAX_STYLE_DEPTH 4
typedef struct {
	Font *default_font;

	IMGUI_Widget widgets[IMGUI_MAX_WIDGETS];
	uint32_t widget_count;

	struct {
		float2 position, last_position;
		bool pressed[3];
		bool released[3];
	} mouse;

	IMGUI_Widget *parent_stack[8];
	uint32_t parent_stack_cursor;

	struct {
		uint64_t id;
		Rectangle rect;
	} cache[IMGUI_MAX_WIDGETS];
	uint32_t cache_count;

	IMGUI_Settings settings_stack[IMGUI_MAX_STYLE_DEPTH];
	uint32_t setting_stack_cursor;

	float time, last_release_time;
	uint64_t last_release_id;
	uint64_t hovered, held;
} IMGUI_Context;

void imgui_frame_begin(IMGUI_Context *context, float dt);
void imgui_frame_end(void);

typedef struct {
	bool hovered, pressed, held, released;
	bool double_release;
} IMGUI_Interact;

IMGUI_Interact imgui_interact(uint64_t id, Rectangle rect);
IMGUI_Widget *imgui_widget(uint64_t id);
IMGUI_Widget *imgui_widget_blank(uint64_t id);
IMGUI_Widget *imgui_widget_ex(uint64_t id, IMGUI_Style style);
IMGUI_Widget *imgui_child(uint64_t id, IMGUI_Widget *parent);
void imgui_parent(IMGUI_Widget *child, IMGUI_Widget *parent);

void imgui_push_parent(IMGUI_Widget *widget);
void imgui_pop_parent(void);

void imgui_push_style(IMGUI_Style style);
void imgui_pop_style(uint32_t count);

IMGUI_Style imgui_peek_style(void);

static inline void imgui_pad(IMGUI_Widget *widget, float4 padding) {
	if (imgui_valid(widget))
		widget->settings.padding[AXIS_X][0] = padding.w, widget->settings.padding[AXIS_X][1] = padding.y, widget->settings.padding[AXIS_Y][0] = padding.x, widget->settings.padding[AXIS_Y][1] = padding.z;
}
static inline Rectangle imgui_rect_live(IMGUI_Widget *widget) {
	return (Rectangle){ widget->offset[0], widget->offset[1], widget->size[0], widget->size[1] };
}
Rectangle imgui_rect_cached(IMGUI_Widget *widget);

typedef enum {
	IMGUI_AXIS_HORIZONTAL = BIT(0),
	IMGUI_AXIS_VERTICAL = BIT(1)
} IMGUI_Axis;

void imgui_fit_tree_along_axis(IMGUI_Widget *root, IMGUI_Axis mask);
void imgui_grow_tree_along_axis(IMGUI_Widget *root, IMGUI_Axis mask);

void imgui_fit_tree(IMGUI_Widget *root);
void imgui_grow_tree(IMGUI_Widget *root);
void imgui_position_tree(IMGUI_Widget *root);

// Common widget helpers
IMGUI_Widget *imgui_label(String8 label);
IMGUI_Interact imgui_button_label(String8 label);
IMGUI_Interact imgui_button_image(Image2D *image, float scale);
IMGUI_Interact imgui_sliderf(uint64_t id, float *t, float min, float max);

IMGUI_Widget *imgui_spacer(void);
