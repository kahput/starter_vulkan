#ifndef UI_H_
#define UI_H_

#include "commands.h"
#include "core/strings.h"

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
	float minimum;
	float preferred;
} UIAxisSize;

typedef enum {
	WIDGET_FLAG_PRESSABLE = 1 << 0,
	WIDGET_FLAG_SCROLLABLE = 1 << 1,

	WIDGET_FLAG_ROUNDED,
	WIDGET_FLAG_BORDER,
	WIDGET_FLAG_TEXT,
	WIDGET_FLAG_IMAGE,

	WIDGET_FLAG_ANIMATE_HOT = 1 << 2,
	WIDGET_FLAG_ANIMATE_ACTIVE = 1 << 3
} UIWidgetFlags;

#define MAX_CHILDREN 32
typedef struct {
	uint32_t id;

	// Hierarchy
	uint32_t parent;
	uint32_t children[MAX_CHILDREN];
	uint32_t children_count;

	float child_offset_accumulator[AXIS2_MAX];

	// Passed
	UIWidgetFlags flags;

	String text;
	Color background_color;
	Axis2 orientation;
	uint16_t padding[2][2];
	uint32_t child_gap;
	UIAxisSize semantic_size[AXIS2_MAX];

	// Computed
	Rectangle rect;
	float offset[AXIS2_MAX];
	float size[AXIS2_MAX];
} UIWidget;

typedef struct {
	bool held;
	bool pressed;
	bool released;
	bool hovering;
} UIInteraction;

#define MAX_DEPTH 8
typedef struct {
	float2 mouse_position;
	bool mouse_down;

	uint32_t depth_parent[MAX_DEPTH];
	uint32_t current_depth;

	UIWidget widgets[MAX_UI_ELEMENTS];
	uint32_t widget_count;

	struct {
		uint32_t id;
		Rectangle rect;
	} cached_widgets[MAX_UI_ELEMENTS];
	uint32_t cached_widget_count;

	int32_t hot_item;
	int32_t active_item;
} UIContext;

#define FIXED(...) ((UIAxisSize){ .type = UI_SIZE_FIXED, __VA_ARGS__ })
#define FIT(...) ((UIAxisSize){ .type = UI_SIZE_FIT, __VA_ARGS__ })
#define GROW(...) ((UIAxisSize){ .type = UI_SIZE_GROW, __VA_ARGS__ })

void ui_frame_begin(UIContext *context);
void ui_frame_end(DrawlistBuffer *buffer);

void ui_widget_push(uint32_t id, UIAxisSize width, UIAxisSize height);
void ui_widget_pop(void);

void ui_push_row(uint32_t id, UIAxisSize width, UIAxisSize height);
void ui_push_column(uint32_t id, UIAxisSize width, UIAxisSize height);

void ui_background_color(Color color);
void ui_absolute_position(float2 pos);
void ui_padding(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom);
void ui_padding_all(uint16_t padding);
void ui_child_gap(uint16_t gap);

bool ui_hovered(void);
bool ui_held(void);
bool ui_pressed(void);

#define LINE_ID(index) (uint32_t)(__LINE__ << 8) + (index)

#endif /* UI_H_ */
