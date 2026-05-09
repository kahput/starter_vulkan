#ifndef UI_H_
#define UI_H_

#include "core/strings.h"
#include <common.h>
#include <core/arena.h>
#include <core/cmath.h>

#define MAX_UI_ELEMENTS 1024

typedef enum {
	UI_HORIOZONTAL,
	UI_VERTICAL,
} UIDirection;

typedef enum {
	UI_SIZE_FIT,
	UI_SIZE_FIXED,
	UI_SIZE_GROW,
	UI_SIZE_PERCENT
} UISizeType;

typedef struct UISize {
	UISizeType type;
	float value;
} UISize;

typedef struct UIPadding {
	uint16_t left, right, top, bottom;
} UIPadding;

typedef struct UILayoutDescription {
	struct {
		UISize width, height;
	} sizing;
	UIPadding padding;
	uint16_t child_gap;
	UIDirection direction;
} UILayout;

typedef struct {
	UILayout layout;
	Color background_color;
} UIElementDesc;

#define MAX_CHILDREN 32
typedef struct {
	int32_t id;
	UIElementDesc description;

	float2 offset, size;
	Color color;

	float2 child_offset;

	uint32_t parent;
	uint32_t children[MAX_CHILDREN];
	uint32_t children_count;
} UIElement;

typedef struct {
	int32_t id;
	float2 position, size;
	Color color;
} UIElementData;

typedef struct {
	Color panel;

	Color button_idle;
	Color button_hover;
	Color button_pressed;

	Color track;
	Color fill;
	Color thumb_idle;
	Color thumb_hover;
	Color thumb_pressed;

	Color text;
	Color text_muted;
	Color accent;
	Color danger;
	Color success;

	float button_height;
	float slider_height;
	float label_height;
} UITheme;

extern UITheme UI_DARK;

#define MAX_DEPTH 8
typedef struct {
	float2 mouse_position;
	bool mouse_down;

	uint32_t depth_parent[MAX_DEPTH];
	uint32_t current_depth;

	UIElement elements[MAX_UI_ELEMENTS]; // 0 == invalid
	uint32_t element_count;

	UIElementData previous_elements[MAX_UI_ELEMENTS];
	uint32_t previous_element_count;

	int32_t hot_item;
	int32_t active_item;

} UIContext;

#define FIXED(n) ((UISize){ UI_SIZE_FIXED, (float)(n) })
#define GROW ((UISize){ UI_SIZE_GROW, 0.0f })
#define FIT ((UISize){ UI_SIZE_FIT, 0.0f })

#define PAD(n) ((UIPadding){ n, n, n, n })
#define PAD_XY(x, y) ((UIPadding){ x, x, y, y })
#define PAD4(l, r, t, b) ((UIPadding){ l, r, t, b })

UIContext ui_make(void);

void ui_frame_begin(UIContext *context);
void ui_frame_end(void);

void ui_begin_element(int32_t ui_id, UIElementDesc element);
void ui_end_element(void);

bool ui_hovered(int32_t id);
bool ui_held(int32_t id);
bool ui_pressed(int32_t id);

bool ui_button(int32_t ui_id, String label, UITheme *theme);
bool ui_push_slider(int32_t ui_id, int32_t max, int32_t value);

#define UI_ID(index) __LINE__ + index
#define ui_container(...) for (uint8_t _ = (ui_begin_element(UI_ID(0), (WRAPPER_STRUCT(UIElementDesc)){ __VA_ARGS__ }.wrapped), 0); !_; (_++, ui_end_element()))

/* bool ui_button(int32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h); */

#endif /* UI_H_ */
