#include "ui.h"
#include "commands.h"
#include "common.h"
#include "core/debug.h"
#include "input.h"
#include <asm-generic/errno-base.h>
#include <math.h>

static UIContext *context = NULL;

static void shrink_and_grow_children(UIElement *parent);
static void wrap_text(UIElement *element);

UITheme UI_DARK = {
	.panel = { 30, 30, 30, 220 },
	.button_idle = { 55, 55, 60, 255 },
	.button_hover = { 80, 80, 88, 255 },
	.button_pressed = { 100, 80, 160, 255 },
	.button_height = 200.f,
	.slider_thickness = 32.f,

	.track = { 30, 30, 30, 220 },
	.thumb_idle = { 55, 55, 60, 255 },
	.thumb_hover = { 80, 80, 88, 255 },
	.thumb_pressed = { 100, 80, 160, 255 },
	.fill = { 100, 80, 160, 255 },
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

void ui_frame_end(DrawlistBuffer *buffer) {
	if (context->mouse_down == 0)
		context->active_item = 0;
	else if (context->active_item == 0)
		context->active_item = -1;

	// Shrink & Grow
	for (uint32_t index = 1; index < context->element_count; ++index)
		shrink_and_grow_children(&context->elements[index]);

	// Wrap text
	for (uint32_t index = 1; index < context->element_count; ++index) {
		UIElement *element = &context->elements[index];
		if (element->description.text.length)
			wrap_text(element);
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
			.text = element->description.text,
			.color = element->color,
			.image = element->description.background_image,
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

	element->size.x = description.layout.sizing.width.size.min;
	element->size.y = description.layout.sizing.height.size.min;

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

	if (element->description.layout.sizing.width.type == UI_SIZE_FIT) {
		element->size.x += padding.left + padding.right;
		element->description.layout.sizing.width.size.min += padding.left + padding.right;
	}
	if (element->description.layout.sizing.height.type == UI_SIZE_FIT) {
		element->size.y += padding.top + padding.bottom;
		element->description.layout.sizing.height.size.min += padding.top + padding.bottom;
	}

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
				if (parent->description.layout.sizing.width.type == UI_SIZE_FIT) {
					parent->size.x += element->size.x;
					parent->description.layout.sizing.width.size.min += element->description.layout.sizing.width.size.min;
				}
				if (parent->description.layout.sizing.height.type == UI_SIZE_FIT) {
					parent->size.y = maxf(parent->size.y, element->size.y);

					UIAxisSize element_height = element->description.layout.sizing.height;
					parent->description.layout.sizing.height.size.min =
						maxf(parent->description.layout.sizing.height.size.min, element_height.size.min);
				}
				break;

			case UI_VERTICAL:
				if (parent->description.layout.sizing.width.type == UI_SIZE_FIT) {
					parent->size.x = maxf(parent->size.x, element->size.x);

					UIAxisSize element_width = element->description.layout.sizing.width;
					parent->description.layout.sizing.width.size.min =
						maxf(parent->description.layout.sizing.width.size.min, element_width.size.min);
				}
				if (parent->description.layout.sizing.height.type == UI_SIZE_FIT) {
					parent->size.y += element->size.y;
					parent->description.layout.sizing.height.size.min += element->description.layout.sizing.height.size.min;
				}

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

void ui_image(int32_t id, RhiTexture texture, float2 size, Color tint) {
	ui_begin_element(id,
		(UIElementDesc){
		  .background_image = texture,
		  .background_color = tint,
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

float ui_sliderf(int32_t id, float value, float min, float max, UITheme *theme) {
	UIElementData *data = ui_find_previous(id);
	UIInteraction ix = ui_interact(id);

	uint32_t axis = UI_HORIZONTAL;
	if (context->current_depth > 0)
		axis = context->elements[context->depth_parent[context->current_depth]].description.layout.direction;
	ASSERT(axis == 0 || axis == 1);

	float result = value;
	if (ix.held && data) {
		float t = ((&context->mouse_position.x)[axis] - (&data->position.x)[axis]) / (&data->size.x)[axis];
		result = min + clampf(t, 0.0f, 1.0f) * (max - min);
	}

	float t = (max > min) ? CLAMP((result - min) / (max - min), 0.0f, 1.0f) : 0.0f;
	float track_main = data ? (&data->size.x)[axis] : 0.0f;
	float thumb_main = theme->slider_thickness; // square thumb
	float fill_main = fmaxf(0.0f, track_main * t - thumb_main);

	Color thumb = ix.held ? theme->thumb_pressed
		: ix.hovered	  ? theme->thumb_hover
						  : theme->thumb_idle;

	float2 fill_size = { 0 }, thumb_size = { 0 };

	(&fill_size.x)[axis] = fill_main;
	(&fill_size.x)[axis ^ 1] = theme->slider_thickness;

	(&thumb_size.x)[axis] = thumb_main;
	(&thumb_size.x)[axis ^ 1] = theme->slider_thickness;

	UIAxisSize sizing[2];
	sizing[axis] = GROW;
	sizing[axis ^ 1] = FIXED(theme->slider_thickness);

	ui_begin_element(id, (UIElementDesc){
						   .background_color = theme->track,
						   .layout = {
							 .sizing = { sizing[0], sizing[1] },
							 .direction = axis,
						   },
						 });

	ui_rect(id ^ 0x1, fill_size, theme->fill);
	ui_rect(id ^ 0x2, thumb_size, thumb);
	ui_end_element();

	return result;
}

void ui_text(int32_t id, Font *font, String text) {
	float width = 0.0f, height = 0.0f;
	for (const char *c = text.chars; *c; ++c) {
		ASSERT(*c >= 32 && *c <= 126);
		Glyph *glyph = &font->glyphs[(uint8_t)*c];
		width += glyph->advance_x;

		if (height == 0.0f)
			height = glyph->atlas_rect.height;

		if (*c == '\n') {
			height += font->line_height;
			continue;
		}
	}

	ui_begin_element(id,
		(UIElementDesc){
		  .text = text,
		  .layout = {
			.sizing = { FIXED(width), FIXED(height) },
		  },
		});
	ui_end_element();
}

void shrink_and_grow_children(UIElement *parent) {
	if (parent->children_count == 0)
		return;

	float2 remaining_size = parent->size;
	UIPadding padding = parent->description.layout.padding;
	remaining_size.x -= (padding.left + padding.right);
	remaining_size.y -= (padding.top + padding.bottom);

	bool is_horizontal = parent->description.layout.direction == UI_HORIZONTAL;

	uint32_t grow_main[MAX_CHILDREN] = { 0 };
	uint32_t shrink_main[MAX_CHILDREN] = { 0 };
	uint32_t grow_cross[MAX_CHILDREN] = { 0 };
	uint32_t grow_main_count = 0, shrink_main_count = 0, grow_cross_count = 0;

	for (uint32_t index = 0; index < parent->children_count; ++index) {
		UIElement *child = &context->elements[parent->children[index]];

		bool grows_x = child->description.layout.sizing.width.type == UI_SIZE_GROW;
		bool fits_x = child->description.layout.sizing.width.type == UI_SIZE_FIT;
		bool grows_y = child->description.layout.sizing.height.type == UI_SIZE_GROW;
		bool fits_y = child->description.layout.sizing.height.type == UI_SIZE_FIT;

		if (is_horizontal) {
			remaining_size.x -= child->size.x;
			if (grows_x)
				grow_main[grow_main_count++] = parent->children[index];
			if (grows_x || fits_x)
				shrink_main[shrink_main_count++] = parent->children[index];
			if (grows_y)
				grow_cross[grow_cross_count++] = parent->children[index];
		} else {
			remaining_size.y -= child->size.y;
			if (grows_y)
				grow_main[grow_main_count++] = parent->children[index];
			if (grows_y || fits_y)
				shrink_main[shrink_main_count++] = parent->children[index];
			if (grows_x)
				grow_cross[grow_cross_count++] = parent->children[index];
		}
	}

	float total_gap = (parent->children_count - 1) * parent->description.layout.child_gap;
	if (is_horizontal) {
		remaining_size.x -= total_gap;
	} else {
		remaining_size.y -= total_gap;
	}

	float *remaining_main = is_horizontal ? &remaining_size.x : &remaining_size.y;
	while (*remaining_main > 0.1f && grow_main_count > 0) {
		float smallest = is_horizontal
			? context->elements[grow_main[0]].size.x
			: context->elements[grow_main[0]].size.y;
		float second_smallest = INFINITY;
		uint32_t num_smallest = 1;

		for (uint32_t index = 1; index < grow_main_count; ++index) {
			UIElement *child = &context->elements[grow_main[index]];
			float size = is_horizontal ? child->size.x : child->size.y;

			if (size == smallest) {
				num_smallest++;
			} else if (size < smallest) {
				second_smallest = smallest;
				smallest = size;
				num_smallest = 1;
			} else if (size < second_smallest)
				second_smallest = size;
		}

		float space_to_add = *remaining_main / num_smallest;
		if (second_smallest != INFINITY)
			space_to_add = minf(space_to_add, second_smallest - smallest);

		for (uint32_t i = 0; i < grow_main_count; ++i) {
			UIElement *child = &context->elements[grow_main[i]];
			float *size = is_horizontal ? &child->size.x : &child->size.y;

			if (*size == smallest) {
				*size += space_to_add;
				*remaining_main -= space_to_add;
			}
		}
	}

	// CROSS AXIS: Stretch to fill parent
	float cross_space = is_horizontal ? remaining_size.y : remaining_size.x;

	for (uint32_t i = 0; i < grow_cross_count; ++i) {
		UIElement *child = &context->elements[grow_cross[i]];
		if (is_horizontal) {
			child->size.y = cross_space;
		} else {
			child->size.x = cross_space;
		}
	}

	// Shrink
	while (*remaining_main < -0.1f && shrink_main_count > 0) {
		float largest = is_horizontal
			? context->elements[shrink_main[0]].size.x
			: context->elements[shrink_main[0]].size.y;
		float second_largest = 0.0f;
		uint32_t num_largest = 1;

		for (uint32_t index = 1; index < shrink_main_count; ++index) {
			UIElement *child = &context->elements[shrink_main[index]];
			float size = is_horizontal ? child->size.x : child->size.y;

			if (size == largest) {
				num_largest++;
			} else if (size > largest) {
				second_largest = largest;
				largest = size;
				num_largest = 1;
			} else if (size > second_largest)
				second_largest = size;
		}

		float space_to_add = *remaining_main / num_largest;
		if (second_largest > 0.0f)
			space_to_add = maxf(space_to_add, second_largest - largest);

		for (uint32_t index = 0; index < shrink_main_count; ++index) {
			UIElement *child = &context->elements[shrink_main[index]];
			float *size = is_horizontal ? &child->size.x : &child->size.y;
			float min_size = is_horizontal
				? child->description.layout.sizing.width.size.min
				: child->description.layout.sizing.height.size.min;

			if (*size == largest) {
				space_to_add = maxf(space_to_add, min_size - *size);
				*size += space_to_add;
				*remaining_main -= space_to_add;
			}
		}

		uint32_t active_count = 0;
		for (uint32_t index = 0; index < shrink_main_count; ++index) {
			UIElement *child = &context->elements[shrink_main[index]];
			float size = is_horizontal ? child->size.x : child->size.y;
			float min_size = is_horizontal
				? child->description.layout.sizing.width.size.min
				: child->description.layout.sizing.height.size.min;

			if (size > min_size + 0.001f) {
				shrink_main[active_count++] = shrink_main[index];
			}
		}
		shrink_main_count = active_count;
	}
}

void wrap_text(UIElement *element) {
	if (element->parent == 0)
		return;

	UIElement *parent = &context->elements[element->parent];

	return;
}
