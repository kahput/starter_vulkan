#include "ui.h"
#include "commands.h"
#include "common.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/strings.h"
#include <ctype.h>
#include <math.h>
#include <stdbool.h>

UIContext *context = NULL;

UIWidget *widget_peek(void) {
	return &context->widgets[context->depth_parent[context->current_depth]];
}
UIWidget *widget_find(uint64_t id) {
	for (uint32_t index = 0; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (widget->id == id)
			return widget;
	}

	return NULL;
}

uint32_t widget_peek_index(void) {
	return context->depth_parent[context->current_depth];
}

UIWidgetCache *find_cached_widget(uint64_t id);
UIInteraction imgui_interact(uint64_t id);

void imgui_frame_begin(UIContext *ctx) {
	context = ctx;
	context->widget_count = 1;
	context->hot_item = 0;
}

static inline void remaining_size(UIWidget *widget, float size[AXIS2_MAX]);
static void fit_children(UIWidget *widget, Axis2 axis, bool is_main);
static void shrink_and_grow_children(UIWidget *widget, Axis2 axis, bool is_main);
static inline void wrap_text(UIWidget *widget);

void imgui_frame_end(DrawlistBuffer *buffer) {
	// Fit Sizing Width
	for (uint32_t index = context->widget_count - 1; index >= 1; --index) {
		UIWidget *widget = &context->widgets[index];
		fit_children(widget, AXIS2_X, widget->orientation == AXIS2_X);

		widget->size[AXIS2_X] = maxf(widget->size[AXIS2_X], widget->semantic_size[AXIS2_X].min);
	}

	// Grow Sizing Width
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		shrink_and_grow_children(widget, AXIS2_X, widget->orientation == AXIS2_X);
	}

	// Wrap
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (FLAG_GET(widget->flags, WIDGET_FLAG_TEXT) == false)
			continue;

		wrap_text(widget);
	}

	// Fit Sizing Height
	for (uint32_t index = context->widget_count - 1; index >= 1; --index) {
		UIWidget *widget = &context->widgets[index];
		fit_children(widget, AXIS2_Y, widget->orientation == AXIS2_Y);
		widget->size[AXIS2_Y] = maxf(widget->size[AXIS2_Y], widget->semantic_size[AXIS2_Y].min);
	}

	// Grow Sizing Height
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		shrink_and_grow_children(widget, AXIS2_Y, widget->orientation == AXIS2_Y);
	}

	// Position & Align
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (widget->parent == 0)
			continue;

		UIWidget *parent = &context->widgets[widget->parent];

		widget->offset[AXIS2_X] += parent->offset[AXIS2_X] + parent->padding[AXIS2_X][0] + parent->child_offset_accumulator[AXIS2_X];
		widget->offset[AXIS2_Y] += parent->offset[AXIS2_Y] + parent->padding[AXIS2_Y][1] + parent->child_offset_accumulator[AXIS2_Y];

		// Alignment
		float remaining[AXIS2_MAX] = { 0 };
		remaining_size(parent, remaining);

		uint32_t main = parent->orientation;
		uint32_t cross = !parent->orientation;
		float scalar[AXIS2_MAX] = {
			parent->align[AXIS2_X] ? parent->align[AXIS2_X] == UI_ALIGN_RIGHT ? 1.0f : 0.5f : 0.0f,
			parent->align[AXIS2_Y] ? parent->align[AXIS2_Y] == UI_ALIGN_BOTTOM ? 1.0f : 0.5f : 0.0f,
		};
		widget->offset[main] += remaining[main] * scalar[main];
		widget->offset[cross] += (remaining[cross] - widget->size[cross]) * scalar[cross];

		parent->child_offset_accumulator[main] += widget->size[main] + parent->child_gap;
	};

	// Draw
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];

		float2 position = { widget->offset[AXIS2_X], widget->offset[AXIS2_Y] };
		float2 size = { widget->size[AXIS2_X], widget->size[AXIS2_Y] };
		widget->rect = (Rectangle){
			position.x, position.y, size.x, size.y
		};

		if (FLAG_GET(widget->flags, WIDGET_FLAG_BACKGROUND)) {
			bool is_hot = widget->id == context->hot_item;
			bool is_active = widget->id == context->active_item;

			if (is_hot && is_active && widget->flags & WIDGET_FLAG_ANIMATE) {
				position.x += 2;
				position.y += 2;

				drawlist_push_rectv(buffer, position, size, widget->background_color);
			} else if (is_active && FLAG_GET(widget->flags, WIDGET_FLAG_ANIMATE_ACTIVE)) {
				position.x += 2;
				position.y += 2;

				drawlist_push_rectv(buffer, position, size, widget->background_color);
			} else if (is_hot && FLAG_GET(widget->flags, WIDGET_FLAG_ANIMATE_HOT)) {
				drawlist_push_rectv(buffer, position, size, widget->background_color);
			} else {
				drawlist_push_rect(buffer, widget->rect, widget->background_color);
			}
		}

		if (FLAG_GET(widget->flags, WIDGET_FLAG_TEXT)) {
			float2 padded_text_position = {
				position.x + widget->padding[AXIS2_X][0],
				position.y + widget->padding[AXIS2_Y][0],
			};
			drawlist_push_text(buffer, widget->font, string_wrap(widget->output_string), padded_text_position, widget->text_color);
		}
	}

	context->cached_widget_count = 0;
	for (uint32_t index = 0; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (widget->flags & WIDGET_FLAG_INTERACTABLE) {
			UIWidgetCache *cached = &context->cached_widgets[context->cached_widget_count++];
			cached->id = widget->id;
			cached->rect = widget->rect;

			context->cached_widget_count++;
		}
	}

	if (context->mouse_left == 0 && context->mouse_right == 0)
		context->active_item = 0;
	else if (context->active_item == 0)
		context->active_item = -1;

	memory_zero_array(context->widgets);
	context = NULL;
}

UIWidget *widget_push(uint64_t id, UIWidgetFlags flags) {
	uint32_t current_index = context->widget_count++;
	UIWidget *widget = &context->widgets[current_index];
	widget->id = id;
	widget->flags = flags;

	if (FLAG_GET(flags, WIDGET_FLAG_ABSOLUTE) == false)
		if (context->current_depth) {
			UIWidget *parent = widget_peek();

			parent->children[parent->children_count++] = current_index;
			widget->parent = widget_peek_index();
		}

	context->depth_parent[++context->current_depth] = current_index;

	return widget;
}

void imgui_widget_push(uint64_t id, UIAxisSize width, UIAxisSize height, UIWidgetFlags flags) {
	UIWidget *widget = widget_push(id, flags);

	widget->semantic_size[AXIS2_X] = width;
	widget->semantic_size[AXIS2_Y] = height;

	widget->semantic_size[AXIS2_X].max = widget->semantic_size[AXIS2_X].max <= 0.0f ? FLOAT_MAX : widget->semantic_size[AXIS2_X].max;
	widget->semantic_size[AXIS2_Y].max = widget->semantic_size[AXIS2_Y].max <= 0.0f ? FLOAT_MAX : widget->semantic_size[AXIS2_Y].max;

	UIWidgetCache *cached = find_cached_widget(id);
	if (cached) {
		widget->size[AXIS2_X] = cached->rect.width;
		widget->size[AXIS2_Y] = cached->rect.height;
	}

	if (widget->flags & WIDGET_FLAG_INTERACTABLE)
		imgui_interact(widget->id);
}

void imgui_widget_pop(void) {
	context->current_depth--;
}

void imgui_background_color(Color color) {
	UIWidget *widget = widget_peek();

	widget->flags |= WIDGET_FLAG_BACKGROUND;
	widget->background_color = color;
}

void imgui_absolute_position(float x, float y) {
	UIWidget *widget = widget_peek();

	widget->flags |= WIDGET_FLAG_ABSOLUTE;
	widget->offset[AXIS2_X] = x;
	widget->offset[AXIS2_Y] = y;
}

void imgui_offset(float x, float y) {
	UIWidget *widget = widget_peek();

	widget->offset[AXIS2_X] = x;
	widget->offset[AXIS2_Y] = y;
}

void imgui_orientation(Axis2 axis) {
	UIWidget *widget = widget_peek();

	widget->orientation = axis;
}

void imgui_align_x(UIAlign align) {
	widget_peek()->align[AXIS2_X] = align;
}
void imgui_align_y(UIAlign align) {
	widget_peek()->align[AXIS2_Y] = align;
}

void imgui_padding(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom) {
	UIWidget *widget = widget_peek();

	widget->padding[AXIS2_X][0] = left;
	widget->padding[AXIS2_X][1] = right;
	widget->padding[AXIS2_Y][0] = top;
	widget->padding[AXIS2_Y][1] = bottom;
}
void imgui_padding_all(uint16_t padding) {
	imgui_padding(padding, padding, padding, padding);
}

void imgui_child_gap(uint16_t gap) {
	UIWidget *widget = widget_peek();

	widget->child_gap = gap;
}

void imgui_rect(uint64_t id, float width, float height, Color color) {
	imgui_widget_push(id, FIXED(width), FIXED(height), WIDGET_FLAG_BACKGROUND);
	imgui_background_color(color);
	imgui_widget_pop();
}

static void measure_text(String text, Font *font, uint32_t *min_width, uint32_t *preferred_width, uint32_t *height) {
	*height = font->line_height;

	uint32_t current_word = 0, largest_word = 0;
	for (uint32_t index = 0; index < text.length; ++index) {
		char c = text.chars[index];
		if (c == '\n') {
			*height += font->line_height;
			ASSERT(c >= 32 && c < 127);
		}

		Glyph *glyph = &font->glyphs[(uint8_t)c];
		*preferred_width += glyph->advance_x;

		if (isalnum(c))
			current_word += glyph->advance_x;
		else {
			largest_word = MAX(current_word, largest_word);
			current_word = 0;
		}
	}
	*min_width = MAX(current_word, largest_word);
}

void imgui_text(String text, Font *font, Color color) {
	UIWidget *widget = widget_push(string_hash64(text), WIDGET_FLAG_TEXT);
	widget->text = text;
	widget->text_color = color;
	widget->font = font;

	uint32_t preferred_width = 0, minimum_width = 0, height = 0;

	measure_text(text, font, &minimum_width, &preferred_width, &height);

	widget->semantic_size[AXIS2_X] = GROW(.min = minimum_width, .max = preferred_width);
	widget->semantic_size[AXIS2_Y] = FIXED(height);

	/* widget->size[AXIS2_X] = preferred_width; */
	/* widget->size[AXIS2_Y] = height; */

	imgui_widget_pop();
}

UIWidgetCache *find_cached_widget(uint64_t id) {
	for (uint32_t index = 0; index < context->cached_widget_count; ++index) {
		UIWidgetCache *cache = &context->cached_widgets[index];
		if (cache->id == id)
			return cache;
	}

	return NULL;
}

UIInteraction imgui_interact(uint64_t id) {
	UIWidget *widget = widget_peek();
	UIInteraction interact = { 0 };
	if ((widget->flags & WIDGET_FLAG_INTERACTABLE) == 0)
		return interact;

	UIWidgetCache *cache = find_cached_widget(id);
	if (cache == NULL)
		return interact;

	float2 mouse = context->mouse_position;
	bool hovered =
		cache->rect.x <= mouse.x && cache->rect.x + cache->rect.width > mouse.x &&
		cache->rect.y <= mouse.y && cache->rect.y + cache->rect.height > mouse.y;

	if (hovered) {
		context->hot_item = id;
		interact.hovering = true;

		if (context->active_item == 0 && (context->mouse_left || context->mouse_right)) {
			interact.held = true;

			context->press_offset = (float2){
				mouse.x - cache->rect.x,
				mouse.y - cache->rect.y,
			};
			context->active_item = id;
		}
	}

	if (context->mouse_left == 0 &&
		context->hot_item == id &&
		context->active_item == id) {
		interact.left_clicked = true;
		cache->resizing = false;
	}

	return interact;
}

UIInteraction imgui_button(String label, Font *font) {
	uint64_t id = string_hash64(label);

	UIWidget *widget = widget_push(id,
		WIDGET_FLAG_CLICKABLE |
			WIDGET_FLAG_BACKGROUND |
			WIDGET_FLAG_BORDER |
			WIDGET_FLAG_TEXT |
			WIDGET_FLAG_ANIMATE_HOT |
			WIDGET_FLAG_ANIMATE_ACTIVE);

	widget->text = label;
	widget->font = font;
	widget->background_color = rgb(0, 0, 0);
	widget->text_color = rgb(255, 255, 255);
	uint32_t padding = 8;
	imgui_padding_all(padding);


	uint32_t preferred_width = 0, minimum_width = 0, height = 0;
	measure_text(label, font, &minimum_width, &preferred_width, &height);

	widget->semantic_size[AXIS2_X] = GROW(.min = minimum_width + widget->padding[AXIS2_X][0] + widget->padding[AXIS2_X][1]);
	widget->semantic_size[AXIS2_Y] = FIXED(height + widget->padding[AXIS2_Y][0] + widget->padding[AXIS2_Y][0]);

	/* widget->semantic_size[AXIS2_X].max = widget->semantic_size[AXIS2_X].max <= 0.0f ? FLOAT_MAX : widget->semantic_size[AXIS2_X].max; */
	/* widget->semantic_size[AXIS2_Y].max = widget->semantic_size[AXIS2_Y].max <= 0.0f ? FLOAT_MAX : widget->semantic_size[AXIS2_Y].max; */

	widget->size[AXIS2_X] = preferred_width;
	widget->size[AXIS2_Y] = height;

	UIInteraction interaction = imgui_interact(id);
	imgui_widget_pop();

	return interaction;
}

bool imgui_hovered(void) {
	return imgui_interact(widget_peek()->id).hovering;
}

bool imgui_held(void) {
	return imgui_interact(widget_peek()->id).held;
}

bool imgui_clicked(void) {
	return imgui_interact(widget_peek()->id).left_clicked;
}
bool imgui_right_clicked(void) {
	return imgui_interact(widget_peek()->id).right_clicked;
}

bool imgui_hot(void) {
	return context->hot_item == widget_peek()->id;
}
bool imgui_active(void) {
	return context->active_item == widget_peek()->id;
}

float2 mouse_position(void);

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
	uint32_t child_gap = widget->children_count ? ((widget->children_count - 1) * widget->child_gap) * is_main : 0;

	float new_size = 0;
	float new_min = 0;

	for (uint32_t index = 0; index < widget->children_count; ++index) {
		UIWidget *child = &context->widgets[widget->children[index]];

		if (is_main) {
			new_size += child->size[axis];
			new_min += child->semantic_size[axis].min;
		} else {
			new_size = maxf(new_size, child->size[axis]);
			new_min = maxf(new_min, child->semantic_size[axis].min);
		}
	}

	new_size += (padding + child_gap);
	new_min += (padding + child_gap);

	widget->size[axis] = maxf(widget->size[axis], new_size);
	widget->semantic_size[axis].min = maxf(widget->semantic_size[axis].min, new_min);

	if (widget->semantic_size[axis].max > 0.1f) {
		widget->size[axis] = minf(widget->semantic_size[axis].max, widget->size[axis]);
		widget->semantic_size[axis].min = minf(widget->semantic_size[axis].max, widget->semantic_size[axis].min);
	}
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

	if (resizeable_count == 0)
		return;

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
					space_to_add = minf(space_to_add, child->size[axis] - child->semantic_size[axis].min);
				if (sign > 0.0f && child->semantic_size[axis].max > 0.1f)
					space_to_add = minf(space_to_add, child->semantic_size[axis].max - child->size[axis]);

				child->size[axis] += space_to_add * sign;
				remaining[axis] -= space_to_add;
			}
		}

		uint32_t active_count = 0;
		for (uint32_t index = 0; index < resizeable_count; ++index) {
			UIWidget *child = resizeable[index];

			bool can_shrink = (sign < 0.0f && child->size[axis] > child->semantic_size[axis].min + 0.01f);
			bool can_grow = (sign > 0.0f && child->size[axis] < child->semantic_size[axis].max - 0.01f);

			if (can_shrink || can_grow) {
				resizeable[active_count++] = child;
			}
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
		if (index < widget->text.length - 1 && c == '#' && widget->text.chars[index + 1] == '#') {
			widget->output_string[index] = '\0';
			break;
		}

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
