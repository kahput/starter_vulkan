#include "imgui.h"
#include "commands.h"
#include "common.h"
#include "core/arena.h"
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
UIWidget *find_widget(uint64_t id) {
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

void imgui_frame_begin(UIContext *ctx) {
	context = ctx;
	context->widget_count = 1;
	context->hot_item = 0;
}

static inline void remaining_size(UIWidget *widget, float size[2], bool ignore_padding);
static void fit_children(UIWidget *widget, Axis2 axis, bool is_main);
static void shrink_and_grow_children(UIWidget *widget, Axis2 axis, bool is_main);
static inline void wrap_text(UIWidget *widget);

static inline void draw_widget(DrawlistBuffer *buffer, UIWidget *widget);

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
		if (FLAG_GET(widget->flags, IMGUI_FLAG_TEXT) == false)
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

		bool is_overlay = FLAG_GET(widget->flags, IMGUI_FLAG_OVERLAY); // Ignore padding and accumulation if overlay

		widget->offset[AXIS2_X] += parent->offset[AXIS2_X] + ((parent->padding[AXIS2_X][0] + parent->child_offset_accumulator[AXIS2_X]) * (!is_overlay));
		widget->offset[AXIS2_Y] += parent->offset[AXIS2_Y] + ((parent->padding[AXIS2_Y][0] + parent->child_offset_accumulator[AXIS2_Y]) * (!is_overlay));

		// Alignment
		float remaining[AXIS2_MAX] = { 0 };
		remaining_size(parent, remaining, is_overlay);

		uint32_t main = parent->orientation;
		uint32_t cross = !parent->orientation;

		ASSERT(parent->anchor >= IMGUI_ANCHOR_TOPLEFT && parent->anchor < IMGUI_ANCHOR_MAX);
		uint32_t align_x = (parent->anchor % 3);
		uint32_t align_y = (parent->anchor / 3);

		float scalar[AXIS2_MAX] = {
			align_x * 0.5f,
			align_y * 0.5f,
		};
		widget->offset[main] += remaining[main] * scalar[main];
		widget->offset[cross] += (remaining[cross] - widget->size[cross]) * scalar[cross];

		parent->child_offset_accumulator[main] += widget->size[main] + parent->child_gap;
	};

	// Draw
	ArenaTemp scratch = arena_scratch_begin(NULL);
	UIWidget **floating_widgets = NULL;
	for (uint32_t index = 1; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (FLAG_GET(widget->flags, IMGUI_FLAG_FLOATING)) {
			arena_darray_put(scratch.arena, floating_widgets, UIWidget *, widget);
			for (uint32_t index = 0; index < widget->children_count; ++index) {
				UIWidget *child = &context->widgets[widget->children[index]];
				child->flags |= IMGUI_FLAG_FLOATING;
			}
			continue;
		}

		draw_widget(buffer, widget);
	}

	for (uint32_t index = 0; index < arena_array_count(floating_widgets); ++index) {
		UIWidget *widget = floating_widgets[index];

		draw_widget(buffer, widget);
	}
	arena_scratch_end(scratch);

	// Cache
	context->cached_widget_count = 0;
	for (uint32_t index = 0; index < context->widget_count; ++index) {
		UIWidget *widget = &context->widgets[index];
		if (widget->flags & IMGUI_FLAG_INTERACTABLE) {
			UIWidgetCache *cached = &context->cached_widgets[context->cached_widget_count++];
			cached->id = widget->id;
			cached->outer = widget->rect;

			cached->inner = widget->rect;

			cached->inner.x -= widget->padding[AXIS2_X][0];
			cached->inner.y -= widget->padding[AXIS2_Y][0];

			cached->inner.width -= widget->padding[AXIS2_X][0] + widget->padding[AXIS2_X][1];
			cached->inner.height -= widget->padding[AXIS2_Y][0] + widget->padding[AXIS2_Y][1];

			context->cached_widget_count++;
		}
	}

	context->last_pressed_id = 0, context->last_released_id = 0;
	if (context->mouse_left == 0 && context->mouse_right == 0) {
		context->active_item = 0;

		if (context->drag_drop_toggle == false) {
			context->drag_drop_data_id = 0;
			context->drag_drop_active = false;
			context->drag_drop_data = NULL;
			context->drag_drop_source_id = 0;
		}
	} else if (context->active_item == 0)
		context->active_item = -1;

	memory_zero_array(context->widgets);
	context = NULL;
}

UIWidget *widget_push(uint64_t id, ImguiFlags flags) {
	UIWidget *widget = find_widget(id);

	if (widget == NULL) {
		uint32_t current_index = context->widget_count++;
		widget = &context->widgets[current_index];
		widget->id = id;
		widget->flags = flags;

		if ((flags & (IMGUI_FLAG_ABSOLUTE | IMGUI_FLAG_FLOATING)) == 0)
			if (context->current_depth) {
				UIWidget *parent = widget_peek();

				parent->children[parent->children_count++] = current_index;
				widget->parent = widget_peek_index();
			}
	}

	context->depth_parent[++context->current_depth] = indexof(context->widgets, widget);
	return widget;
}

void imgui_widget_begin(uint64_t id, UIAxisSize width, UIAxisSize height, ImguiFlags flags) {
	UIWidget *widget = widget_push(id, flags);

	widget->semantic_size[AXIS2_X] = width;
	widget->semantic_size[AXIS2_Y] = height;

	widget->semantic_size[AXIS2_X].max = widget->semantic_size[AXIS2_X].max <= 0.0f ? FLOAT_MAX : widget->semantic_size[AXIS2_X].max;
	widget->semantic_size[AXIS2_Y].max = widget->semantic_size[AXIS2_Y].max <= 0.0f ? FLOAT_MAX : widget->semantic_size[AXIS2_Y].max;
}

void imgui_widget_end(void) {
	UIWidget *widget = widget_peek();
	context->current_depth--;
}

void imgui_background_color(Color color) {
	UIWidget *widget = widget_peek();

	widget->flags |= IMGUI_FLAG_BACKGROUND;
	widget->background_color = color;
}

void imgui_background_image(Texture2D image) {
	UIWidget *widget = widget_peek();

	widget->flags |= IMGUI_FLAG_IMAGE;
	widget->image = image;
}

bool imgui_drag_data(uint64_t data_id, uint64_t data_size, void *data) {
	if (context->drag_drop_active == false) {
		if (context->last_pressed_id == widget_peek()->id) {
			context->last_pressed_id = 0;
			context->drag_drop_active = true;
			context->drag_drop_data_id = data_id;
			context->drag_drop_data = data;
			context->drag_drop_toggle = FLAG_GET(widget_peek()->flags, IMGUI_FLAG_TOGGLEABLE);
			context->drag_drop_source_id = widget_peek()->id;
		}
	}

	return context->drag_drop_source_id == widget_peek()->id;
}

void *imgui_drop_data(uint64_t data_id) {
	if (context->drag_drop_active == false || context->drag_drop_data_id != data_id)
		return NULL;

	bool dropped = false;
	if (context->drag_drop_toggle == false)
		dropped = context->last_released_id == widget_peek()->id;
	else {
		dropped = context->last_pressed_id == widget_peek()->id;
		context->last_pressed_id = 0;
	}

	void *data = NULL;
	if (dropped) {
		data = context->drag_drop_data;
		context->drag_drop_data_id = 0;
		context->drag_drop_active = false;
		context->drag_drop_data = NULL;
		context->drag_drop_source_id = 0;
	}

	return data;
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

void imgui_anchor(ImguiAnchor anchor) {
	widget_peek()->anchor = anchor;
}

void imgui_padding(uint16_t left, uint16_t right, uint16_t top, uint16_t bottom) {
	UIWidget *widget = widget_peek();

	widget->padding[AXIS2_X][0] = left;
	widget->padding[AXIS2_X][1] = right;
	widget->padding[AXIS2_Y][0] = top;
	widget->padding[AXIS2_Y][1] = bottom;
}

void imgui_padding_x(uint16_t padding) {
	UIWidget *widget = widget_peek();
	imgui_padding(padding, padding, widget->padding[AXIS2_Y][0], widget->padding[AXIS2_Y][1]);
}
void imgui_padding_y(uint16_t padding) {
	UIWidget *widget = widget_peek();
	imgui_padding(widget->padding[AXIS2_X][0], widget->padding[AXIS2_X][1], padding, padding);
}

void imgui_padding_xy(uint16_t padding) {
	imgui_padding(padding, padding, padding, padding);
}

void imgui_child_gap(uint16_t gap) {
	UIWidget *widget = widget_peek();

	widget->child_gap = gap;
}

Rectangle imgui_rect_last_frame(uint64_t id) {
	UIWidgetCache *cache = find_cached_widget(id);
	if (cache)
		return cache->outer;

	return (Rectangle){ 0 };
}

bool imgui_is_active(uint64_t id) {
	return id == context->active_item;
}
bool imgui_is_hot(uint64_t id) {
	return id == context->hot_item;
}

float2 imgui_mouse_position(void) {
	return context->mouse_position;
}

void imgui_widget_rect(uint64_t id, float width, float height, Color color) {
	imgui_widget_begin(id, FIXED(width), FIXED(height), IMGUI_FLAG_BACKGROUND);
	imgui_background_color(color);
	imgui_widget_end();
}

void imgui_widget_image(uint64_t id, Texture2D texture) {
	imgui_widget_begin(id, FIXED(texture.width), FIXED(texture.height), IMGUI_FLAG_BACKGROUND);
	imgui_background_image(texture);
	imgui_widget_end();
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

void imgui_widget_text(String text, Font *font, Color color, ImguiFlags flags) {
	UIWidget *widget = widget_push(string_hash64(text), IMGUI_FLAG_TEXT | flags);
	widget->text = text;
	widget->text_color = color;
	widget->font = font;

	uint32_t preferred_width = 0, minimum_width = 0, height = 0;

	measure_text(text, font, &minimum_width, &preferred_width, &height);

	widget->semantic_size[AXIS2_X] = GROW(.min = minimum_width, .max = preferred_width);
	widget->semantic_size[AXIS2_Y] = FIXED(height);

    // NOTE: This is needed because height isn't clamped to min before FIT sizing,
    // but is increased at WRAP 
	widget->size[AXIS2_Y] = height;

	imgui_widget_end();
}

UIWidgetCache *find_cached_widget(uint64_t id) {
	for (uint32_t index = 0; index < context->cached_widget_count; ++index) {
		UIWidgetCache *cache = &context->cached_widgets[index];
		if (cache->id == id)
			return cache;
	}

	return NULL;
}

ImguiInteraction imgui_interact(uint64_t id, Rectangle area) {
	ImguiInteraction interact = { 0 };

	float2 mouse = context->mouse_position;
	bool hovered = rect_contains(area, mouse.x, mouse.y);

	if (hovered) {
		context->hot_item = id;
		interact.hovering = true;

		if (context->active_item == 0 && context->mouse_left) {
			interact.pressed = context->mouse_left;
			context->last_pressed_id = id;

			context->active_item = id;
			context->drag_start_position = mouse;
		}
	}

	if (context->active_item == id && context->mouse_left) {
		interact.held = true;

		if (float2_equal(mouse, context->drag_start_position) == false)
			interact.dragging = true;
	}

	if (context->active_item && context->mouse_left == false && hovered)
		context->last_released_id = id;

	if (context->mouse_left == false && context->active_item == id) {
		if (context->hot_item == id)
			interact.clicked = true;
	}

	return interact;
}

ImguiInteraction imgui_button(String label, Font *font) {
	uint64_t id = string_hash64(label);

	UIWidget *widget = widget_push(id,
		IMGUI_FLAG_CLICKABLE |
			IMGUI_FLAG_BACKGROUND |
			IMGUI_FLAG_BORDER |
			IMGUI_FLAG_TEXT |
			IMGUI_FLAG_ANIMATE_HOT |
			IMGUI_FLAG_ANIMATE_ACTIVE);

	widget->text = label;
	widget->font = font;
	widget->background_color = rgb(0, 0, 0);
	widget->text_color = rgb(255, 255, 255);
	uint32_t padding = 8;
	imgui_padding_xy(padding);

	uint32_t preferred_width = 0, minimum_width = 0, height = 0;
	measure_text(label, font, &minimum_width, &preferred_width, &height);

	widget->semantic_size[AXIS2_X] = GROW(.min = minimum_width + widget->padding[AXIS2_X][0] + widget->padding[AXIS2_X][1]);
	widget->semantic_size[AXIS2_Y] = FIXED(height + widget->padding[AXIS2_Y][0] + widget->padding[AXIS2_Y][0]);

	/* widget->semantic_size[AXIS2_X].max = widget->semantic_size[AXIS2_X].max <= 0.0f ? FLOAT_MAX : widget->semantic_size[AXIS2_X].max; */
	/* widget->semantic_size[AXIS2_Y].max = widget->semantic_size[AXIS2_Y].max <= 0.0f ? FLOAT_MAX : widget->semantic_size[AXIS2_Y].max; */

	widget->size[AXIS2_X] = preferred_width;
	widget->size[AXIS2_Y] = height;

	ImguiInteraction interaction = { 0 };

	UIWidgetCache *cache = find_cached_widget(id);
	if (cache)
		interaction = imgui_interact(id, cache->outer);
	imgui_widget_end();

	return interaction;
}

bool imgui_scrollbar(uint64_t id, float *value, float min, float max) {
	uint64_t track_id = id;
	uint64_t thumb_id = hash64_combine(id, shash("thumb"));

	bool changed = false;

	UIWidgetCache *cached = find_cached_widget(track_id);
	if (cached && context->active_item == id) {
		float draggable_height = cached->inner.height;
		float offset_y = clampf(context->mouse_position.y - cached->outer.y, 64, draggable_height - 64);

		float new_value = (offset_y / draggable_height) * max;
		if (new_value != *value) {
			*value = (offset_y / draggable_height) * max;
			changed = true;
		}
	}

	imgui_widget_begin(track_id, FIT(8), GROW(), IMGUI_FLAG_CLICKABLE);
	{
		Color track_color = rgb(80, 80, 80);
		Color thumb_color = rgb(30, 30, 30);

		/* imgui_background_color(track_color); */
		imgui_anchor(IMGUI_ANCHOR_TOPRIGHT);

		float t_slider = clampf(*value, min, max) / max;

		ImguiInteraction interaction = imgui_interact(track_id, cached ? cached->outer : (Rectangle){ 0 });

		imgui_widget_begin(thumb_id, FIXED(4), FIXED(128), 0);
		{
			UIWidget *thumb = widget_peek();
			if (interaction.hovering || interaction.held) {
				thumb->background_color.r -= 10;
				thumb->background_color.g -= 10;
				thumb->background_color.b -= 10;
				/* thumb->offset[AXIS2_X] -= 2; */
				thumb->size[AXIS2_X] += 8;
			}

			float draggable_height = cached ? cached->inner.height : 0.0f;
			float y = clampf((t_slider * draggable_height) - 64, 0.0f, draggable_height);
			imgui_offset(0.0f, y);
			imgui_background_color(thumb_color);
		}
		imgui_widget_end(); // thumb
	}

	return changed;
}

void remaining_size(UIWidget *widget, float size[2], bool ignore_padding) {
	uint32_t main = widget->orientation;
	uint32_t cross = !widget->orientation;

	size[main] = widget->size[main];
	size[main] -= (widget->padding[main][0] + widget->padding[main][1]) * !ignore_padding;
	size[cross] = widget->size[cross];
	size[cross] -= (widget->padding[cross][0] + widget->padding[cross][1]) * !ignore_padding;

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
	remaining_size(parent, remaining, false);

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

void draw_widget(DrawlistBuffer *buffer, UIWidget *widget) {
	float2 position = { widget->offset[AXIS2_X], widget->offset[AXIS2_Y] };
	float2 size = { widget->size[AXIS2_X], widget->size[AXIS2_Y] };
	widget->rect = (Rectangle){
		position.x, position.y, size.x, size.y
	};

	Rectangle inner = {
		.x = position.x + widget->padding[AXIS2_X][0],
		.y = position.y + widget->padding[AXIS2_Y][0],

		.width = size.x - (widget->padding[AXIS2_X][0] + widget->padding[AXIS2_X][1]),
		.height = size.y - (widget->padding[AXIS2_Y][0] + widget->padding[AXIS2_Y][1]),
	};

	if (FLAG_GET(widget->flags, IMGUI_FLAG_BACKGROUND)) {
		bool is_hot = widget->id == context->hot_item;
		bool is_active = widget->id == context->active_item;

		if (is_hot && is_active && widget->flags & IMGUI_FLAG_ANIMATE) {
			position.x += 2;
			position.y += 2;

			drawlist_push_rectv(buffer, position, size, widget->background_color);
		} else if (is_active && FLAG_GET(widget->flags, IMGUI_FLAG_ANIMATE_ACTIVE)) {
			position.x += 2;
			position.y += 2;

			drawlist_push_rectv(buffer, position, size, widget->background_color);
		} else if (is_hot && FLAG_GET(widget->flags, IMGUI_FLAG_ANIMATE_HOT)) {
			drawlist_push_rectv(buffer, position, size, widget->background_color);
		} else {
			drawlist_push_rect(buffer, widget->rect, widget->background_color);
		}
	}

	if (FLAG_GET(widget->flags, IMGUI_FLAG_IMAGE)) {
		ASSERT(widget->image.handle.id);
		Rectangle src = { 0, 0, widget->image.width, widget->image.height };
		drawlist_push_texture_ex(buffer, widget->image.handle, src, inner, (float2){ 0 }, 0.0f, WHITE);
	}

	if (FLAG_GET(widget->flags, IMGUI_FLAG_TEXT)) {
		drawlist_push_text(buffer, widget->font, string_wrap(widget->output_string), position, widget->text_color);
	}
}
