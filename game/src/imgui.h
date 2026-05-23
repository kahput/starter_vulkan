#ifndef UI_H_
#define UI_H_

#include "commands.h"
#include "core/strings.h"
#include "input/input_types.h"

#include <common.h>
#include <core/arena.h>
#include <core/cmath.h>

#include <core/r_types.h>

#define MAX_UI_ELEMENTS 1024

typedef enum {
	AXIS2_X,
	AXIS2_Y,

	AXIS2_MAX,
} Axis2;

typedef enum {
	UI_SIZE_FIT,
	UI_SIZE_FIXED,
	UI_SIZE_GROW,
	UI_SIZE_PERCENT
} UISizeType;

typedef struct UISize {
	UISizeType type;
	float min, max;
} UIAxisSize;

typedef enum {
	IMGUI_ANCHOR_TOPLEFT,
	IMGUI_ANCHOR_TOP,
	IMGUI_ANCHOR_TOPRIGHT,

	IMGUI_ANCHOR_LEFT,
	IMGUI_ANCHOR_CENTER,
	IMGUI_ANCHOR_RIGHT,

	IMGUI_ANCHOR_BOTTOMLEFT,
	IMGUI_ANCHOR_BOTTOM,
	IMGUI_ANCHOR_BOTTOMRIGHT,

	IMGUI_ANCHOR_MAX,
} ImguiAnchor;

typedef enum {
	IMGUI_FLAG_CLICKABLE = 1 << 0,
	IMGUI_FLAG_TOGGLEABLE = 1 << 1,
	IMGUI_FLAG_SCROLLABLE = 1 << 2,
	IMGUI_FLAG_DRAGGABLE = 1 << 3,
	IMGUI_FLAG_RESIZABLE = 1 << 4,

	IMGUI_FLAG_INTERACTABLE =
		IMGUI_FLAG_CLICKABLE |
		IMGUI_FLAG_SCROLLABLE |
		IMGUI_FLAG_DRAGGABLE |
		IMGUI_FLAG_RESIZABLE,

	IMGUI_FLAG_ABSOLUTE = 1 << 5,
	IMGUI_FLAG_OVERLAY = 1 << 6,
	IMGUI_FLAG_BACKGROUND = 1 << 7,
	IMGUI_FLAG_ROUNDED = 1 << 8,
	IMGUI_FLAG_BORDER = 1 << 9,
	IMGUI_FLAG_TEXT = 1 << 10,
	IMGUI_FLAG_IMAGE = 1 << 11,
	IMGUI_FLAG_FLOATING = 1 << 12,

	IMGUI_FLAG_ANIMATE_HOT = 1 << 13,
	IMGUI_FLAG_ANIMATE_ACTIVE = 1 << 14,

	IMGUI_FLAG_ANIMATE =
		IMGUI_FLAG_ANIMATE_HOT | IMGUI_FLAG_ANIMATE_ACTIVE,
} ImguiFlags;

#define MAX_CHILDREN 32
typedef struct {
	uint64_t id;

	// Hierarchy
	uint32_t parent;
	uint32_t children[MAX_CHILDREN];
	uint32_t children_count;

	float child_offset_accumulator[AXIS2_MAX];

	// Passed
	ImguiFlags flags;
	char output_string[256];
	String text;
	Color background_color, text_color;
	Texture2D image;
	Axis2 orientation;
	ImguiAnchor anchor;
	uint16_t padding[2][2];
	uint32_t child_gap;
	UIAxisSize semantic_size[AXIS2_MAX];

	// TEMPORARY
	Font *font;

	// Computed
	Rectangle rect;
	float offset[AXIS2_MAX];
	float size[AXIS2_MAX];
} UIWidget;

typedef struct {
	uint64_t id;
	Rectangle outer, inner;
} UIWidgetCache;

typedef struct {
	bool held, hovering, dragging;
	bool pressed, clicked;
} ImguiInteraction;

#define MAX_DEPTH 8
typedef struct {
	float2 mouse_position;
	bool mouse_left, mouse_right;

	uint32_t depth_parent[MAX_DEPTH];
	uint32_t current_depth;

	UIWidget widgets[MAX_UI_ELEMENTS];
	uint32_t widget_count;

	UIWidgetCache cached_widgets[MAX_UI_ELEMENTS];
	uint32_t cached_widget_count;

	uint64_t hot_item;
	uint64_t active_item;

	float2 drag_start_position;

	bool drag_drop_active, drag_drop_delivering;
	uint64_t drag_drop_data_id, drag_drop_source_id;
	void *drag_drop_data;
} UIContext;

#define FIXED(n) ((UIAxisSize){ .type = UI_SIZE_FIXED, .min = (n), .max = (n) })
#define FIT(...) ((UIAxisSize){ .type = UI_SIZE_FIT, __VA_ARGS__ })
#define GROW(...) ((UIAxisSize){ .type = UI_SIZE_GROW, __VA_ARGS__ })

void imgui_frame_begin(UIContext *context);
void imgui_frame_end(DrawlistBuffer *buffer);

ImguiInteraction imgui_interact(uint64_t id, Rectangle area, ImguiFlags flags);

void imgui_widget_begin(uint64_t id, UIAxisSize width, UIAxisSize height, ImguiFlags flags);
void imgui_widget_end(void);

void imgui_background_color(Color color);
void imgui_background_image(Texture2D image);
void imgui_offset(float x, float y);
void imgui_orientation(Axis2 axis);

void imgui_anchor(ImguiAnchor anchor);

void imgui_padding(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom);
void imgui_padding_x(uint16_t padding);
void imgui_padding_y(uint16_t padding);
void imgui_padding_xy(uint16_t padding);
void imgui_child_gap(uint16_t gap);

bool imgui_is_active(uint64_t id);
bool imgui_is_hot(uint64_t id);

// Drag & Drop
bool imgui_drag_data(uint64_t data_id, uint64_t data_size, void *data);
bool imgui_can_drop_data(uint64_t data_id);
void *imgui_drop_data(void);

Rectangle imgui_rect_last_frame(uint64_t id);
float2 imgui_mouse_position(void);

void imgui_widget_rect(uint64_t id, float width, float height, Color color);
void imgui_widget_image(uint64_t id, Texture2D texture);
void imgui_widget_text(String text, Font *font, Color color, ImguiFlags flags);

ImguiInteraction imgui_button(String label, Font *font);
bool imgui_scrollbar(uint64_t id, float *value, float min, float max);

#define LINE_ID(index) (uint32_t)(__LINE__ << 8) + (index)
#endif /* UI_H_ */
