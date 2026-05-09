#include "ui.h"
#include "common.h"
#include "core/debug.h"
#include "input.h"
#include <math.h>

static UIContext *context = NULL;

UITheme UI_DARK = {
	.panel = { 18, 18, 18, 255 },
	.panel = { 30, 30, 30, 220 },
	.button_idle = { 55, 55, 60, 255 },
	.button_hover = { 80, 80, 88, 255 },
	.button_pressed = { 100, 80, 160, 255 },
    .button_height = 200.f,
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

	// position
	for (uint32_t index = 1; index < context->element_count; ++index) {
		UIElement *element = &context->elements[index];
		if (element->parent == 0)
			continue;

		UIElement *parent = &context->elements[element->parent];

		element->offset.x += parent->offset.x + parent->description.layout.padding.left;
		element->offset.y += parent->offset.y + parent->description.layout.padding.top;

		element->offset = float2_add(element->offset, parent->child_offset);

		if (parent->description.layout.direction == UI_HORIOZONTAL)
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

static void grow_children(UIElement *parent) {
	if (parent->children_count == 0)
		return;

	float2 remaining_size = parent->size;

	UIPadding padding = parent->description.layout.padding;

	remaining_size.x -= padding.left + padding.right;
	remaining_size.y -= padding.top + padding.bottom;

	uint32_t growable[MAX_CHILDREN] = { 0 };
	uint32_t growable_x_count = 0, growable_y_count = 0;
	uint32_t growable_count = 0;

	for (uint32_t index = 0; index < parent->children_count; ++index) {
		UIElement *child = &context->elements[parent->children[index]];

		if (parent->description.layout.direction == UI_HORIOZONTAL) {
			remaining_size.x -= child->size.x;
		} else
			remaining_size.y -= child->size.y;

		if (child->description.layout.sizing.width.type == UI_SIZE_GROW)
			growable[growable_x_count++ + growable_y_count] = parent->children[index];
		if (child->description.layout.sizing.height.type == UI_SIZE_GROW)
			growable[growable_x_count + growable_y_count++] = parent->children[index];
	}

	if (parent->description.layout.direction == UI_HORIOZONTAL)
		remaining_size.x -= (parent->children_count - 1) * (float)parent->description.layout.child_gap;
	else
		remaining_size.y -= (parent->children_count - 1) * (float)parent->description.layout.child_gap;

	growable_count = growable_x_count + growable_y_count;
	if (growable_count == 0)
		return;

	while (remaining_size.x > 0.0f) {
		if (growable_x_count == 0)
			break;
		float smallest = context->elements[growable[0]].size.x;
		float second_smallest = INFINITY;
		float width_to_add = remaining_size.x;

		for (uint32_t growable_index = 0; growable_index < growable_count; ++growable_index) {
			UIElement *child = &context->elements[growable[growable_index]];
			if (child->description.layout.sizing.width.type != UI_SIZE_GROW)
				continue;

			if (child->size.x < smallest) {
				second_smallest = smallest;
				smallest = child->size.x;
			}
			if (child->size.x > smallest) {
				second_smallest = minf(second_smallest, child->size.x);
				width_to_add = second_smallest - smallest;
			}
		}

		width_to_add = minf(width_to_add, remaining_size.x / growable_count);

		for (uint32_t growable_index = 0; growable_index < growable_count; ++growable_index) {
			UIElement *child = &context->elements[growable[growable_index]];
			if (child->description.layout.sizing.width.type != UI_SIZE_GROW)
				continue;

			if (child->size.x == smallest) {
				child->size.x += width_to_add;
				remaining_size.x -= width_to_add;
			}
		}
	}

	while (remaining_size.y > 0.0f) {
		if (growable_y_count == 0)
			break;

		float smallest = context->elements[growable[0]].size.y;
		float second_smallest = INFINITY;
		float height_to_add = remaining_size.y;

		for (uint32_t growable_index = 0; growable_index < growable_count; ++growable_index) {
			UIElement *child = &context->elements[growable[growable_index]];
			if (child->description.layout.sizing.height.type != UI_SIZE_GROW)
				continue;

			if (child->size.y < smallest) {
				second_smallest = smallest;
				smallest = child->size.y;
			}
			if (child->size.y > smallest) {
				second_smallest = minf(second_smallest, child->size.y);
				height_to_add = second_smallest - smallest;
			}
		}

		height_to_add = minf(height_to_add, remaining_size.y / growable_count);

		for (uint32_t growable_index = 0; growable_index < growable_count; ++growable_index) {
			UIElement *child = &context->elements[growable[growable_index]];
			if (child->description.layout.sizing.height.type != UI_SIZE_GROW)
				continue;

			if (child->size.y == smallest) {
				child->size.y += height_to_add;
				remaining_size.y -= height_to_add;
			}
		}
	}
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

	if (element->parent) {
		UIElement *parent = &context->elements[element->parent];
		float child_gap = (parent->children_count - 1) * parent->description.layout.child_gap;

		switch (parent->description.layout.direction) {
			case UI_HORIOZONTAL:
				if (parent->description.layout.sizing.width.type == UI_SIZE_FIT) {
					parent->size.x += child_gap;
					parent->size.x += element->size.x;
				}
				if (parent->description.layout.sizing.height.type == UI_SIZE_FIT)
					parent->size.y = maxf(parent->size.y, element->size.y);
				break;

			case UI_VERTICAL:
				if (parent->description.layout.sizing.width.type == UI_SIZE_FIT)
					parent->size.x = maxf(parent->size.x, element->size.x);
				if (parent->description.layout.sizing.height.type == UI_SIZE_FIT) {
					parent->size.y += child_gap;
					parent->size.y += element->size.y;
				}

				break;
		}
	}

	grow_children(element);
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
			.direction = UI_HORIOZONTAL,
		  },
		});

	/* ui_label(id ^ 0x1, label, theme->text); */

	ui_end_element();
	return ix.pressed;
}
