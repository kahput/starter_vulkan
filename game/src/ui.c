#include "ui.h"
#include "commands.h"
#include "common.h"
#include "core/cmath.h"
#include "core/debug.h"
#include <ctype.h>
#include <math.h>

UIContext *context = NULL;

UIWidget *widget_peek(void) {
	return &context->widgets[context->depth_parent[context->current_depth]];
}

uint32_t widget_peek_index(void) {
	return context->depth_parent[context->current_depth];
}

void ui_frame_begin(UIContext *ctx) {
	context = ctx;
	context->widget_count = 1;
}

static inline void remaining_size(UIWidget *widget, float size[AXIS2_MAX]);
static void fit_children(UIWidget *widget, Axis2 axis, bool is_main);
static void shrink_and_grow_children(UIWidget *widget, Axis2 axis, bool is_main);
static inline void wrap_text(UIWidget *widget);

void ui_frame_end(DrawlistBuffer *buffer) {
	// Fit Sizing Width
	for (uint32_t index = context->widget_count - 1; index >= 1; --index) {
		UIWidget *widget = &context->widgets[index];
		if (widget->children_count == 0)
			continue;

		fit_children(widget, AXIS2_X, widget->orientation == AXIS2_X);
	}

	// Grow Sizing Width
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (widget->children_count == 0)
			continue;

		shrink_and_grow_children(widget, AXIS2_X, widget->orientation == AXIS2_X);
	}

	// Wrap
	for (uint32_t index = 0; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (FLAG_GET(widget->flags, WIDGET_FLAG_TEXT) == false)
			continue;

		wrap_text(widget);
	}

	// Fit Sizing Height
	for (uint32_t index = context->widget_count - 1; index >= 1; --index) {
		UIWidget *widget = &context->widgets[index];
		if (widget->children_count == 0)
			continue;

		fit_children(widget, AXIS2_Y, widget->orientation == AXIS2_Y);
	}

	// Grow Sizing Height
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (widget->children_count == 0)
			continue;

		shrink_and_grow_children(widget, AXIS2_Y, widget->orientation == AXIS2_Y);
	}

	// Position & Align
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index + 1];
		if (widget->parent == 0)
			continue;

		UIWidget *parent = &context->widgets[widget->parent];

		widget->offset[AXIS2_X] += parent->offset[AXIS2_X] + parent->padding[AXIS2_X][0] + parent->child_offset_accumulator[AXIS2_X];
		widget->offset[AXIS2_Y] += parent->offset[AXIS2_Y] + parent->padding[AXIS2_Y][1] + parent->child_offset_accumulator[AXIS2_Y];

		// Alignment
		float remaining[AXIS2_MAX] = { 0 };
		remaining_size(parent, remaining);

		if (parent->id == 223744) {
			uint32_t x = 0;
			(void)x;
		}

		uint32_t main = parent->orientation;
		uint32_t cross = !parent->orientation;
		widget->offset[main] += remaining[main] * 0.5f;
		widget->offset[cross] += (remaining[cross] - widget->size[cross]) * 0.5f;

		parent->child_offset_accumulator[parent->orientation] += widget->size[parent->orientation] + parent->child_gap;
	}

	// Draw
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];

		float2 position = { widget->offset[AXIS2_X], widget->offset[AXIS2_Y] };

		widget->rect = (Rectangle){
			position.x, position.y, widget->size[AXIS2_X], widget->size[AXIS2_Y]
		};
		if (FLAG_GET(widget->flags, WIDGET_FLAG_TEXT)) {
			drawlist_push_text(buffer, widget->font, string_wrap(widget->output_string), position, widget->color);
		} else {
			drawlist_push_rect(buffer, widget->rect, widget->color);
		}

		context->cached_widgets[index].id = widget->id;
		context->cached_widgets[index].rect = widget->rect;
	}

	context->cached_widget_count = 0;
	for (uint32_t index = 0; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		context->cached_widgets[context->cached_widget_count].id = widget->id;
		context->cached_widgets[context->cached_widget_count].rect = widget->rect;

		context->cached_widget_count++;
	}

	memory_zero_array(context->widgets);
	context = NULL;
}

UIWidget *widget_push(uint32_t id) {
	uint32_t current_index = context->widget_count++;
	UIWidget *widget = &context->widgets[current_index];
	widget->id = id;

	if (context->current_depth) {
		UIWidget *parent = widget_peek();

		parent->children[parent->children_count++] = current_index;
		widget->parent = widget_peek_index();
	}

	context->depth_parent[++context->current_depth] = current_index;

	return widget;
}

void ui_widget_push(uint32_t id, UIAxisSize width, UIAxisSize height) {
	UIWidget *widget = widget_push(id);

	widget->semantic_size[AXIS2_X] = width;
	widget->semantic_size[AXIS2_Y] = height;

	widget->size[AXIS2_X] = width.minimum;
	widget->size[AXIS2_Y] = height.minimum;
}

void ui_widget_pop(void) {
	UIWidget *widget = widget_peek();
	context->current_depth--;
}

void ui_push_row(uint32_t id, UIAxisSize width, UIAxisSize height) {
	UIWidget *widget = widget_push(id);
	widget->orientation = AXIS2_X;

	widget->semantic_size[AXIS2_X] = width;
	widget->semantic_size[AXIS2_Y] = height;

	widget->size[AXIS2_X] = width.minimum;
	widget->size[AXIS2_Y] = height.minimum;
}

void ui_push_column(uint32_t id, UIAxisSize width, UIAxisSize height) {
	UIWidget *widget = widget_push(id);
	widget->orientation = AXIS2_Y;

	widget->semantic_size[AXIS2_X] = width;
	widget->semantic_size[AXIS2_Y] = height;

	widget->size[AXIS2_X] = width.minimum;
	widget->size[AXIS2_Y] = height.minimum;
}

void ui_pop(void) {
	context->current_depth--;
}

void ui_background_color(Color color) {
	UIWidget *widget = widget_peek();

	widget->color = color;
}

void ui_absolute_position(float2 pos) {
	UIWidget *widget = widget_peek();

	widget->offset[AXIS2_X] = pos.x;
	widget->offset[AXIS2_Y] = pos.y;
}

void ui_orientation(Axis2 axis) {
	UIWidget *widget = widget_peek();

	widget->orientation = axis;
}

void ui_padding(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom) {
	UIWidget *widget = widget_peek();

	widget->padding[AXIS2_X][0] = left;
	widget->padding[AXIS2_X][1] = right;
	widget->padding[AXIS2_Y][0] = top;
	widget->padding[AXIS2_Y][1] = bottom;
}
void ui_padding_all(uint16_t padding) {
	ui_padding(padding, padding, padding, padding);
}

void ui_child_gap(uint16_t gap) {
	UIWidget *widget = widget_peek();

	widget->child_gap = gap;
}

void ui_rect(uint32_t id, float width, float height, Color color) {
	ui_widget_push(id, FIXED(width), FIXED(height));
	ui_background_color(color);
	ui_widget_pop();
}

void ui_text(uint32_t id, String text, Font *font, Color color) {
	uint32_t minimum_width = 0, preferred_width = 0, height = font->line_height;

	uint32_t current_word = 0, largest_word = 0;
	for (uint32_t index = 0; index < text.length; ++index) {
		char c = text.chars[index];
		if (c == '\n')
			height += font->line_height;
		ASSERT(c >= 32 && c < 127);

		Glyph *glyph = &font->glyphs[(uint8_t)c];
		preferred_width += glyph->advance_x;

		if (isalnum(c))
			current_word += glyph->advance_x;
		else {
			largest_word = MAX(current_word, largest_word);
			current_word = 0;
		}
	}
	minimum_width = largest_word;

	UIWidget *widget = widget_push(id);
	widget->flags = WIDGET_FLAG_TEXT;
	widget->text = text;
	widget->color = color;
	widget->font = font;

	widget->semantic_size[AXIS2_X] = GROW(.minimum = minimum_width, .preferred = preferred_width);
	widget->semantic_size[AXIS2_Y] = FIXED(height);
	widget->size[AXIS2_X] = preferred_width;
	widget->size[AXIS2_Y] = height;
	ui_widget_pop();
}

void remaining_size(UIWidget *widget, float size[2]) {
	uint32_t main = widget->orientation;
	uint32_t cross = !widget->orientation;

	size[main] = widget->size[main];
	size[main] -= widget->padding[main][0] + widget->padding[main][1];
	size[cross] = widget->size[cross];
	size[cross] -= widget->padding[cross][0] + widget->padding[cross][1];

	for (uint32_t index = 0; index < widget->children_count; ++index) {
		UIWidget *child = &context->widgets[widget->children[index]];
		size[main] -= child->size[main];
	}
	size[main] -= (widget->children_count - 1) * widget->child_gap;
}

void fit_children(UIWidget *widget, Axis2 axis, bool is_main) {
	bool fit = widget->semantic_size[axis].type == UI_SIZE_FIT || widget->semantic_size[axis].type == UI_SIZE_GROW;
	if (fit == false)
		return;

	// Fit
	uint32_t padding = widget->padding[axis][0] + widget->padding[axis][1];
	uint32_t child_gap = ((widget->children_count - 1) * widget->child_gap);

	if (is_main)
		widget->size[axis] += (padding + child_gap);

	for (uint32_t index = 0; index < widget->children_count; ++index) {
		UIWidget *child = &context->widgets[widget->children[index]];

		if (is_main) {
			widget->size[axis] += child->size[axis];
			widget->semantic_size[axis].minimum += child->semantic_size[axis].minimum;
		} else {
			widget->size[axis] = MAX(widget->size[axis], child->size[axis]);
			widget->semantic_size[axis].minimum = MAX(widget->semantic_size[axis].minimum, child->semantic_size[axis].minimum);
		}
	}

	if (!is_main)
		widget->size[axis] += padding;
}

void shrink_and_grow_children(UIWidget *parent, Axis2 axis, bool is_main) {
	float remaining[AXIS2_MAX] = { 0 };
	remaining_size(parent, remaining);

	UIWidget *resizeable[MAX_CHILDREN] = { 0 };
	uint32_t resizeable_count = { 0 };
	bool is_shrinking = remaining[axis] < 0.0f;

	for (uint32_t index = 0; index < parent->children_count; ++index) {
		UIWidget *child = &context->widgets[parent->children[index]];

		if (child->semantic_size[axis].type == UI_SIZE_GROW)
			resizeable[resizeable_count++] = child;
		else if (is_shrinking && child->semantic_size[axis].type == UI_SIZE_FIT) {
			resizeable[resizeable_count++] = child;
		}
	}

	if (is_main == false) {
		for (uint32_t index = 0; index < resizeable_count; ++index) {
			UIWidget *child = resizeable[index];
			child->size[axis] = maxf(child->size[axis], remaining[axis]);
		}

		return;
	}

	float sign = signf(remaining[axis]);
	remaining[axis] = fabsf(remaining[axis]);
	while (remaining[axis] > 0.01f && resizeable_count) {
		float smallest = resizeable[0]->size[axis] * sign;
		float second_smallest = INFINITY;
		uint32_t smallest_count = 1;

		for (uint32_t index = 1; index < resizeable_count; ++index) {
			float size = resizeable[index]->size[axis] * sign;

			if (size == smallest) {
				smallest_count++;
			} else if (size < smallest) {
				second_smallest = smallest;
				smallest = size;
				smallest_count = 1;
			} else if (size < second_smallest) {
				second_smallest = size;
			}
		}

		float space_to_add = remaining[axis] / smallest_count;
		if (second_smallest != INFINITY)
			space_to_add = minf(space_to_add, second_smallest - smallest);

		for (uint32_t index = 0; index < resizeable_count; ++index) {
			UIWidget *child = resizeable[index];
			if (child->size[axis] * sign == smallest) {
				if (sign < 0.0f)
					space_to_add = minf(space_to_add, child->size[axis] - child->semantic_size[axis].minimum);

				child->size[axis] += space_to_add * sign;
				remaining[axis] -= space_to_add;
			}
		}

		uint32_t active_count = 0;
		for (uint32_t index = 0; index < resizeable_count; ++index) {
			UIWidget *child = resizeable[index];

			if (sign > 0.0f || child->size[axis] > child->semantic_size[axis].minimum + 0.01f)
				resizeable[active_count++] = child;
		}
		resizeable_count = active_count;
	}
}

void wrap_text(UIWidget *widget) {
	uint32_t current_width = 0;
	int32_t last_space = -1;
	uint32_t width_at_last_space = 0;

	uint32_t length = MIN(sizeof(widget->output_string) - 1, widget->text.length);
	memory_copy(widget->output_string, widget->text.chars, length);
	widget->output_string[length] = '\0';

	for (uint32_t index = 0; widget->output_string[index]; ++index) {
		char c = widget->text.chars[index];

		if (c == '\n') {
			current_width = 0;
			last_space = -1;
			width_at_last_space = 0;
			continue;
		}

		if (c == ' ') {
			last_space = (int32_t)index;
			width_at_last_space = current_width;
		}
		current_width += widget->font->glyphs[(uint8_t)c].advance_x;

		if (current_width > widget->size[AXIS2_X]) {
			if (last_space >= 0) {
				widget->output_string[last_space] = '\n';

				current_width -= width_at_last_space;
				last_space = -1;
				width_at_last_space = 0;
				widget->size[AXIS2_Y] += widget->font->line_height;
			}
		}
	}
}
