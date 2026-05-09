#include "ui.h"
#include "common.h"
#include "core/debug.h"
#include "input.h"
#include <math.h>

static UIContext *context = NULL;

static void grow_children(UIElement *parent);

UITheme UI_DARK = {
	.panel = { 30, 30, 30, 220 },
	.button_idle = { 55, 55, 60, 255 },
	.button_hover = { 80, 80, 88, 255 },
	.button_pressed = { 100, 80, 160, 255 },
	.button_height = 200.f,
	.slider_height = 32.f,

	.track = { 30, 30, 30, 220 },
	.thumb_idle = { 55, 55, 60, 255 },
	.thumb_hover = { 80, 80, 88, 255 },
	.thumb_pressed = { 100, 80, 160, 255 },
	.text = { 220, 220, 220, 255 },
	.accent = { 130, 100, 210, 255 },
};

UIContext ui_make(void) {
	UIContext result = {
		.element_count = 1,
	};

	return result;
}

void ui_frame_begin(UIContext *ctx) {
	context = ctx;

	context->mouse_position = float2_from_double2(input_mouse_position());
	context->mouse_down = input_mouse_down(MOUSE_BUTTON_LEFT);
	context->hot_item = 0;
}

void ui_frame_end(void) {
	if (context->mouse_down == 0)
		context->active_item = 0;
	else if (context->active_item == 0)
		context->active_item = -1;

	// Grow
	for (uint32_t index = 1; index < context->element_count; ++index) {
		grow_children(&context->elements[index]);
	}

	// position
	for (uint32_t index = 1; index < context->element_count; ++index) {
		UIElement *element = &context->elements[index];
		if (element->parent == 0)
			continue;

		UIElement *parent = &context->elements[element->parent];

		element->offset.x += parent->offset.x + parent->description.layout.padding.left;
		element->offset.y += parent->offset.y + parent->description.layout.padding.top;

		element->offset = float2_add(element->offset, parent->child_offset);

		if (parent->description.layout.direction == UI_HORIZONTAL)
			parent->child_offset.x += element->size.x + parent->description.layout.child_gap;
		else
			parent->child_offset.y += element->size.y + parent->description.layout.child_gap;
	}

	context->previous_element_count = 0;
	memory_zero_array(context->previous_elements);

	for (uint32_t index = 1; index < context->element_count; ++index) {
		UIElement *element = &context->elements[index];

		ASSERT(context->previous_element_count < countof(context->previous_elements));
		context->previous_elements[context->previous_element_count++] = (UIElementData){
			.id = element->id,
			.position = element->offset,
			.size = element->size,
			.color = element->color,
		};
	}

	context->element_count = 1;
	memory_zero_array(context->elements);

	context = NULL;
}

void ui_begin_element(int32_t ui_id, UIElementDesc description) {
	size_t max_elements = countof(context->elements);
	ASSERT(context->element_count < countof(context->elements));
	uint32_t current_index = context->element_count++;
	UIElement *element = &context->elements[current_index];
	element->id = ui_id;
	element->description = description;

	element->color = description.background_color;

	element->size.x = description.layout.sizing.width.value;
	element->size.y = description.layout.sizing.height.value;

	if (context->current_depth > 0) {
		element->parent = context->depth_parent[context->current_depth];
		UIElement *parent = &context->elements[element->parent];
		parent->children[parent->children_count++] = current_index;
	}

	ASSERT(context->current_depth < countof(context->depth_parent));
	context->depth_parent[++context->current_depth] = current_index;
}

void ui_end_element(void) {
	size_t max_elements = countof(context->elements);
	uint32_t current_index = context->depth_parent[context->current_depth];
	UIElement *element = &context->elements[current_index];

	UIPadding padding = element->description.layout.padding;

	if (element->description.layout.sizing.width.type == UI_SIZE_FIT)
		element->size.x += padding.left + padding.right;
	if (element->description.layout.sizing.height.type == UI_SIZE_FIT)
		element->size.y += padding.top + padding.bottom;

	if (element->children_count) {
		float child_gap = (element->children_count - 1) * element->description.layout.child_gap;

		if (element->description.layout.direction == UI_HORIZONTAL) {
			if (element->description.layout.sizing.width.type == UI_SIZE_FIT)
				element->size.x += child_gap;
		} else {
			if (element->description.layout.sizing.height.type == UI_SIZE_FIT) {
				element->size.y += child_gap;
			}
		}
	}

	if (element->parent) {
		UIElement *parent = &context->elements[element->parent];

		switch (parent->description.layout.direction) {
			case UI_HORIZONTAL:
				if (parent->description.layout.sizing.width.type == UI_SIZE_FIT)
					parent->size.x += element->size.x;
				if (parent->description.layout.sizing.height.type == UI_SIZE_FIT)
					parent->size.y = maxf(parent->size.y, element->size.y);
				break;

			case UI_VERTICAL:
				if (parent->description.layout.sizing.width.type == UI_SIZE_FIT)
					parent->size.x = maxf(parent->size.x, element->size.x);
				if (parent->description.layout.sizing.height.type == UI_SIZE_FIT)
					parent->size.y += element->size.y;

				break;
		}
	}
	context->current_depth--;
}

static UIElementData *ui_find_previous(int32_t id) {
	for (uint32_t i = 0; i < context->previous_element_count; ++i)
		if (context->previous_elements[i].id == id)
			return &context->previous_elements[i];

	return NULL;
}

typedef struct {
	bool hovered;
	bool held;
	bool pressed; // true the frame mouse is released over the element
} UIInteraction;

static UIInteraction ui_interact(int32_t id) {
	UIElementData *prev = ui_find_previous(id);
	UIInteraction result = { 0 };
	if (prev == NULL)
		return result;

	float2 mouse = context->mouse_position;
	bool hovered =
		mouse.x >= prev->position.x && mouse.x < prev->position.x + prev->size.x &&
		mouse.y >= prev->position.y && mouse.y < prev->position.y + prev->size.y;

	if (hovered)
		context->hot_item = id;

	if (hovered && context->active_item == 0 && context->mouse_down)
		context->active_item = id;

	result.hovered = hovered;
	result.held = context->active_item == id && context->mouse_down;
	result.pressed = context->active_item == id && context->hot_item == id && context->mouse_down == false;

	return result;
}

bool ui_hovered(int32_t id) {
	return ui_interact(id).hovered;
}

bool ui_held(int32_t id) {
	return ui_interact(id).held;
}

bool ui_pressed(int32_t id) {
	return ui_interact(id).pressed;
}

void ui_rect(int32_t id, float2 size, Color color) {
	ui_begin_element(id,
		(UIElementDesc){
		  .background_color = color,
		  .layout = {
			.sizing = { FIXED(size.x), FIXED(size.y) },
		  },
		});
	ui_end_element();
}

bool ui_button(int32_t id, String label, UITheme *theme) {
	UIInteraction ix = ui_interact(id);

	Color bg = theme->button_idle;
	if (ix.hovered)
		bg = theme->button_hover;
	if (ix.held)
		bg = theme->button_pressed;

	ui_begin_element(id,
		(UIElementDesc){
		  .background_color = bg,
		  .layout = {
			.sizing = { GROW, FIXED(theme->button_height) },
			.padding = PAD_XY(12, 0),
			.direction = UI_HORIZONTAL,
		  },
		});

	/* ui_label(id ^ 0x1, label, theme->text); */

	ui_end_element();
	return ix.pressed;
}

bool ui_sliderf(int32_t id, float *value, float min, float max, UITheme *theme) {
	UIElementData *data = ui_find_previous(id);
	UIInteraction ix = ui_interact(id);

	bool changed = false;
	if (ix.held && data) {
		float t = (context->mouse_position.x - data->position.x) / data->size.x;
		float newv = min + CLAMP(t, 0.0f, 1.0f) * (max - min);
		if (newv != *value) {
			*value = newv;
			changed = true;
		}
	}

	float t = (max > min) ? CLAMP((*value - min) / (max - min), 0.0f, 1.0f) : 0.0f;
	float track_w = data ? data->size.x : 0.0f;
	float thumb_w = theme->slider_height; // square thumb
	float fill_w = fmaxf(0.0f, track_w * t - thumb_w);

	Color thumb = ix.held ? theme->thumb_pressed
		: ix.hovered	  ? theme->thumb_hover
						  : theme->thumb_idle;

	ui_begin_element(id, (UIElementDesc){
						   .background_color = theme->track,
						   .layout = {
							 .sizing = { GROW, FIXED(theme->slider_height) },
							 .direction = UI_HORIZONTAL,
						   },
						 });
	// Fill stretches from left up to just before the thumb centre
	ui_rect(id ^ 0x1, (float2){ fill_w, theme->slider_height }, theme->fill);
	// Thumb sits immediately after the fill, centred on the value position
	ui_rect(id ^ 0x2, (float2){ thumb_w, theme->slider_height }, thumb);
	ui_end_element();

	return changed;
}

void grow_children(UIElement *parent) {
	if (parent->children_count == 0)
		return;

	// base available area inside padding
	float2 remaining_size = parent->size;
	UIPadding padding = parent->description.layout.padding;
	remaining_size.x -= (padding.left + padding.right);
	remaining_size.y -= (padding.top + padding.bottom);

	bool is_horizontal = parent->description.layout.direction == UI_HORIZONTAL;

	uint32_t growable_main[MAX_CHILDREN];
	uint32_t growable_cross[MAX_CHILDREN];
	uint32_t growable_main_count = 0;
	uint32_t growable_cross_count = 0;

	for (uint32_t index = 0; index < parent->children_count; ++index) {
		UIElement *child = &context->elements[parent->children[index]];

		bool grows_x = child->description.layout.sizing.width.type == UI_SIZE_GROW;
		bool grows_y = child->description.layout.sizing.height.type == UI_SIZE_GROW;

		if (is_horizontal) {
			remaining_size.x -= child->size.x;
			if (grows_x)
				growable_main[growable_main_count++] = parent->children[index];
			if (grows_y)
				growable_cross[growable_cross_count++] = parent->children[index];
		} else {
			remaining_size.y -= child->size.y;
			if (grows_y)
				growable_main[growable_main_count++] = parent->children[index];
			if (grows_x)
				growable_cross[growable_cross_count++] = parent->children[index];
		}
	}

	// subtract gaps on the main axis
	float total_gap = (parent->children_count - 1) * parent->description.layout.child_gap;
	if (is_horizontal) {
		remaining_size.x -= total_gap;
	} else {
		remaining_size.y -= total_gap;
	}

	// MAIN AXIS: Equalize and distribute shared space
	float *remaining_main = is_horizontal ? &remaining_size.x : &remaining_size.y;
	while (*remaining_main > 0.1f && growable_main_count > 0) {
		float smallest = INFINITY;

		// find the absolute smallest size
		for (uint32_t i = 0; i < growable_main_count; ++i) {
			UIElement *child = &context->elements[growable_main[i]];
			float size = is_horizontal ? child->size.x : child->size.y;
			if (size < smallest) {
				smallest = size;
			}
		}

		// find the second smallest size and count how many elements share the smallest size
		float second_smallest = INFINITY;
		uint32_t num_smallest = 0;

		for (uint32_t i = 0; i < growable_main_count; ++i) {
			UIElement *child = &context->elements[growable_main[i]];
			float size = is_horizontal ? child->size.x : child->size.y;

			if (size == smallest) {
				num_smallest++;
			} else if (size > smallest && size < second_smallest) {
				second_smallest = size;
			}
		}

		// calculate how much space we can add to the smallest items without passing the second smallest
		float space_needed_to_level = (second_smallest - smallest) * num_smallest;
		float add_per_item = 0;

		if (second_smallest != INFINITY && *remaining_main >= space_needed_to_level) {
			add_per_item = second_smallest - smallest; // Level them up to second_smallest
		} else {
			add_per_item = *remaining_main / (float)num_smallest; // Split whatever is left
		}

		// apply the growth
		for (uint32_t i = 0; i < growable_main_count; ++i) {
			UIElement *child = &context->elements[growable_main[i]];
			float *size = is_horizontal ? &child->size.x : &child->size.y;

			if (*size == smallest) {
				*size += add_per_item;
				*remaining_main -= add_per_item;
			}
		}
	}

	// CROSS AXIS: Stretch to fill parent
	float cross_space = is_horizontal ? remaining_size.y : remaining_size.x;

	for (uint32_t i = 0; i < growable_cross_count; ++i) {
		UIElement *child = &context->elements[growable_cross[i]];
		if (is_horizontal) {
			child->size.y = cross_space;
		} else {
			child->size.x = cross_space;
		}
	}
}
