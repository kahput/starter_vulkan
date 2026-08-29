#include "common.h"
#include "draw/font.h"
#include "draw/imgui.h"

#include "core/debug.h"

static IMGUI_Context *context = 0;
IMGUI_Widget IMGUI_NIL = { 0 };

bool imgui_valid(IMGUI_Widget *node) {
	bool ok = context;
	if (ok)
		ok = node && node != &IMGUI_NIL && node >= context->widgets + 1 && node < context->widgets + context->widget_count;

	return ok;
}

IMGUI_Widget *imgui__widget_from_id(uint64_t id) {
	IMGUI_Widget *result = &IMGUI_NIL;

	bool ok = context;
	if (ok)
		for (uint32_t index = 0; index < context->widget_count; ++index) {
			if (context->widgets[index].id == id) {
				result = &context->widgets[index];
				break;
			}
		}

	return result;
}

Rectangle imgui__cached_from_id(uint64_t id) {
	Rectangle result = { 0 };

	bool ok = context;
	if (ok) {
		for (uint32_t index = 0; index < context->cache_count; ++index) {
			if (context->cache[index].id == id) {
				result = context->cache[index].rect;
				break;
			}
		}
	}

	return result;
}

bool imgui__remove_child(IMGUI_Widget *parent, uint32_t target_index) {
	bool ok = imgui_valid(parent);
	if (ok) {
		int32_t found_index = -1;
		for (uint32_t search_index = 0; search_index < parent->children_count; ++search_index) {
			if (parent->children[search_index] == target_index) {
				found_index = search_index;
				break;
			}
		}

		if (found_index != -1) {
			parent->children[found_index] = parent->children[parent->children_count - 1];
			parent->children_count--;
		}
	}

	return ok;
}

void imgui_frame_begin(IMGUI_Context *ctx, IMGUI_Mouse mouse, float dt) {
	context = ctx;
	context->widget_count = 1;
	context->status.time += dt;
	context->mouse = mouse;
	context->status.hover_t += dt, context->status.press_t += dt;
	context->status.hover_t = clampf(context->status.hover_t, 0.0f, 1.0f);
	context->status.press_t = clampf(context->status.press_t, 0.0f, 1.0f);
	memory_zero_array(context->widgets);
}

void imgui_frame_end(void) {
	if (context->status.active && context->mouse.released[0])
		context->status.active = 0;
	if (context->mouse.pressed[0] && context->status.active == 0)
		context->status.active = -1;
	if (context->status.last_hovered != context->status.hovered)
		context->status.hover_t = 0.0f;
	context->status.last_hovered = context->status.hovered;
	context->status.hovered = 0;

	// cache
	context->cache_count = 1;
	for (uint32_t index = 0; index < context->widget_count; ++index) {
		IMGUI_Widget *widget = &context->widgets[index];

		context->cache[context->cache_count].id = widget->id;
		context->cache[context->cache_count++].rect = imgui_rect_live(widget);
	}

	context->widget_count = 1;
	context = 0;
}

IMGUI_Interact imgui_interact(uint64_t id, Rectangle rect) {
	IMGUI_Interact result = { 0 };

	bool ok = context && id;
	if (ok) {
		if (rect_contains_point(rect, context->mouse.position)) {
			context->status.hovered = id;

			if (context->status.hovered == id && context->mouse.pressed[0] && context->status.active == 0) {
				result.pressed = true;
				context->status.active = id;
				context->status.press_t = 0.0f;
			}
		}

		result.held = context->status.active == id;
		result.hovered = context->status.hovered == id;
		result.released = result.held && result.hovered && context->mouse.released[0];
		result.hover_t = context->status.hover_t;
		result.press_t = context->status.press_t;

		if (result.released && context->status.last_release_id == id && context->status.time - context->status.last_release_time <= 0.5f) {
			result.double_release = true;
			context->status.last_release_time = 0.0f;
			context->status.last_release_id = 0;
		} else if (result.released) {
			context->status.last_release_id = id;
			context->status.last_release_time = context->status.time;
		}
	}

	return result;
}

IMGUI_Widget *imgui_widget_blank(uint64_t id) {
	IMGUI_Widget *result = &IMGUI_NIL;
	bool ok = context;
	if (ok) {
		ASSERT(context->widget_count < countof(context->widgets));
		result = &context->widgets[context->widget_count++];
		result->id = id;
	}

	return result;
}

IMGUI_Widget *imgui_widget_opt(uint64_t id, IMGUI_Style style) {
	IMGUI_Widget *result = &IMGUI_NIL;

	imgui_push_style(style);
	result = imgui_widget(id);
	imgui_pop_style(1);

	return result;
}

IMGUI_Widget *imgui_widget(uint64_t id) {
	IMGUI_Widget *result = imgui_widget_blank(id);
	if (imgui_valid(result)) {
		if (context->setting_stack_cursor)
			result->settings = context->settings_stack[context->setting_stack_cursor - 1];
		if (context->parent_stack_cursor)
			imgui_parent(result, context->parent_stack[context->parent_stack_cursor - 1]);
	}

	return result;
}

IMGUI_Widget *imgui_child(uint64_t id, IMGUI_Widget *parent) {
	IMGUI_Widget *result = &IMGUI_NIL;

	imgui_push_parent(parent);
	result = imgui_widget(id);
	imgui_pop_parent();

	return result;
}

void imgui_parent(IMGUI_Widget *child, IMGUI_Widget *parent) {
	bool ok = context && imgui_valid(child);
	if (ok) {
		uint32_t child_index = indexof(context->widgets, child);
		imgui__remove_child(&context->widgets[child->parent], child_index);
		child->parent = 0;

		if (parent) {
			ASSERT(parent->children_count < countof(parent->children));

			child->parent = indexof(context->widgets, parent);
			parent->children[parent->children_count++] = child_index;
		}
	}
}

void imgui_unparent(IMGUI_Widget *child) {
	bool ok = context && imgui_valid(child) && child->parent > 0 && child->parent < countof(context->widgets);
	if (ok) {
		IMGUI_Widget *parent = &context->widgets[child->parent];
		imgui__remove_child(parent, indexof(context->widgets, child));
	}
}

void imgui_push_parent(IMGUI_Widget *widget) {
	bool ok = context && imgui_valid(widget) && context->parent_stack_cursor < countof(context->parent_stack);
	if (ok)
		context->parent_stack[context->parent_stack_cursor++] = widget;
}
void imgui_pop_parent(void) {
	bool ok = context && context->parent_stack_cursor;
	if (ok)
		context->parent_stack_cursor--;
}

void imgui__parse_style_config(IMGUI_Style *cfg, IMGUI_Settings *style) {
	style->flow = cfg->flow;
	style->sizing[0] = cfg->sizing[0], style->sizing[1] = cfg->sizing[1];
	style->align[0] = cfg->align[0], style->align[1] = cfg->align[1];

	style->bg = cfg->bg, style->fg = cfg->fg, style->border = cfg->border;
	style->border_radius = cfg->border_radius, style->border_width = cfg->border_width;

	// clang-format off
	style->padding[AXIS_X][0] = cfg->pl ? cfg->pl : cfg->ph ? cfg->ph : cfg->p;
	style->padding[AXIS_X][1] = cfg->pr ? cfg->pr : cfg->ph ? cfg->ph : cfg->p;

	style->padding[AXIS_Y][0] = cfg->pt ? cfg->pt : cfg->pv ? cfg->pv : cfg->p;
	style->padding[AXIS_Y][1] = cfg->pb ? cfg->pb : cfg->pv ? cfg->pv : cfg->p;
	// clang-format on

	style->child_gap = cfg->gap;
}

IMGUI_Style imgui_peek_style(void) {
	IMGUI_Style result = { 0 };

	bool ok = context && context->setting_stack_cursor;
	if (ok) {
		IMGUI_Settings settings = context->settings_stack[context->setting_stack_cursor - 1];

		result.flow = settings.flow;
		result.sizing[0] = settings.sizing[0], result.sizing[1] = settings.sizing[1];
		result.align[0] = settings.align[0], result.align[1] = settings.align[1];
		result.bg = settings.bg, result.fg = settings.fg;
		result.border_radius = settings.border_radius;
		result.gap = settings.child_gap;

		result.pl = settings.padding[AXIS_X][0];
		result.pr = settings.padding[AXIS_X][1];
		result.pt = settings.padding[AXIS_Y][0];
		result.pb = settings.padding[AXIS_Y][1];

		if (result.pl == result.pr)
			result.ph = result.pl;
		if (result.pt == result.pb)
			result.pv = result.pt;
		if (result.ph == result.pv)
			result.p = result.ph;

		result.font = settings.font ? settings.font : context->default_font;
	}

	return result;
}

void imgui_push_style(IMGUI_Style style) {
	bool ok = context;
	if (ok) {
		ASSERT(context->setting_stack_cursor < countof(context->settings_stack));
		imgui__parse_style_config(&style, context->settings_stack + context->setting_stack_cursor++);
	}
}
void imgui_pop_style(uint32_t count) {
	bool ok = context && context->setting_stack_cursor;
	if (ok)
		context->setting_stack_cursor = count > context->setting_stack_cursor ? 0 : context->setting_stack_cursor - count;
}

Rectangle imgui_rect_cached(IMGUI_Widget *widget) {
	return imgui__cached_from_id(widget->id);
}

void imgui__remaining_size(IMGUI_Widget *widget, float size[2]) {
	uint32_t main = widget->settings.flow;
	uint32_t cross = !widget->settings.flow;

	size[main] = widget->size[main];
	size[main] -= (widget->settings.padding[main][0] + widget->settings.padding[main][1]);
	size[cross] = widget->size[cross];
	size[cross] -= (widget->settings.padding[cross][0] + widget->settings.padding[cross][1]);

	for (uint32_t index = 0; index < widget->children_count; ++index) {
		IMGUI_Widget *child = &context->widgets[widget->children[index]];
		size[main] -= child->size[main];
	}
	size[main] -= (widget->children_count - 1) * widget->settings.child_gap;
}

void imgui_fit_tree_along_axis(IMGUI_Widget *root, IMGUI_Axis axis) {
	uint32_t main = root->settings.flow;
	uint32_t cross = !root->settings.flow;

	float new_size[AXIS_MAX2D] = { 0 };
	for (uint32_t index = 0; index < root->children_count; ++index) {
		IMGUI_Widget *child = &context->widgets[root->children[index]];
		imgui_fit_tree_along_axis(child, axis);

		new_size[main] += child->size[main];
		new_size[cross] = maxf(new_size[cross], child->size[cross]);
	}

	bool fit[] = {
		root->settings.sizing[AXIS_X] == IMGUI_SIZING_FIT || root->settings.sizing[AXIS_X] == IMGUI_SIZING_GROW,
		root->settings.sizing[AXIS_Y] == IMGUI_SIZING_FIT || root->settings.sizing[AXIS_Y] == IMGUI_SIZING_GROW
	};

	if (fit[main] && has_flag(axis, main + 1)) {
		uint32_t padding = root->settings.padding[main][0] + root->settings.padding[main][1];
		uint32_t child_gap = root->children_count ? ((root->children_count - 1) * root->settings.child_gap) : 0;

		new_size[main] += padding + child_gap;
		root->size[main] = maxf(root->size[main], new_size[main]);
	}
	if (fit[cross] && has_flag(axis, cross + 1)) {
		uint32_t padding = root->settings.padding[cross][0] + root->settings.padding[cross][1];

		new_size[cross] += padding;
		root->size[cross] = maxf(root->size[cross], new_size[cross]);
	}
}

void imgui_grow_tree_along_axis(IMGUI_Widget *root, IMGUI_Axis mask) {
	float remaining[AXIS_MAX2D] = { 0 };
	imgui__remaining_size(root, remaining);
	uint32_t main = root->settings.flow;
	uint32_t cross = !root->settings.flow;

	IMGUI_Widget *growable[AXIS_MAX2D][IMGUI_MAX_CHILDREN] = { 0 };
	uint32_t growable_count[AXIS_MAX2D] = { 0 };

	for (uint32_t index = 0; index < root->children_count; ++index) {
		IMGUI_Widget *child = &context->widgets[root->children[index]];

		if (child->settings.sizing[main] == IMGUI_SIZING_GROW)
			growable[main][growable_count[main]++] = child;
		if (child->settings.sizing[cross] == IMGUI_SIZING_GROW)
			growable[cross][growable_count[cross]++] = child;
	}

	if (has_flag(mask, cross + 1))
		for (uint32_t index = 0; index < growable_count[cross]; ++index) {
			IMGUI_Widget *child = growable[cross][index];
			child->size[cross] = maxf(child->size[cross], remaining[cross]);
		}

	if (has_flag(mask, main + 1) && growable_count[main]) {
		float sign = signf(remaining[main]);
		remaining[main] = fabsf(remaining[main]);
		while (remaining[main] > 0.01f) {
			float smallest = growable[main][0]->size[main] * sign;
			float second_smallest = INFINITY;
			uint32_t smallest_count = 1;

			for (uint32_t index = 1; index < growable_count[main]; ++index) {
				float size = growable[main][index]->size[main] * sign;

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

			float space_to_add = remaining[main] / smallest_count;
			if (second_smallest != INFINITY)
				space_to_add = minf(space_to_add, second_smallest - smallest);

			for (uint32_t index = 0; index < growable_count[main]; ++index) {
				IMGUI_Widget *child = growable[main][index];
				if (child->size[main] * sign == smallest) {
					child->size[main] += space_to_add * sign;
					remaining[main] -= space_to_add;
				}
			}
		}
	}

	// Grow children's children
	for (uint32_t index = 0; index < root->children_count; ++index) {
		IMGUI_Widget *child = &context->widgets[root->children[index]];
		imgui_grow_tree_along_axis(child, mask);
	}
}

void imgui_fit_tree(IMGUI_Widget *root) {
	imgui_fit_tree_along_axis(root, IMGUI_AXIS_HORIZONTAL | IMGUI_AXIS_VERTICAL);
}

void imgui_grow_tree(IMGUI_Widget *root) {
	imgui_grow_tree_along_axis(root, IMGUI_AXIS_HORIZONTAL | IMGUI_AXIS_VERTICAL);
}

void imgui_position_tree(IMGUI_Widget *root) {
	float child_offset_accumulator[AXIS_MAX2D] = { 0 };
	for (uint32_t index = 0; index < root->children_count; ++index) {
		IMGUI_Widget *child = &context->widgets[root->children[index]];

		child->offset[AXIS_X] += root->offset[AXIS_X] + (root->settings.padding[AXIS_X][0] + child_offset_accumulator[AXIS_X]);
		child->offset[AXIS_Y] += root->offset[AXIS_Y] + (root->settings.padding[AXIS_Y][0] + child_offset_accumulator[AXIS_Y]);

		// Alignment
		float remaining[AXIS_MAX2D] = { 0 };
		imgui__remaining_size(root, remaining);

		uint32_t main = root->settings.flow;
		uint32_t cross = !root->settings.flow;

		float scalar[AXIS_MAX2D] = {
			root->settings.align[AXIS_X] ? root->settings.align[AXIS_X] == IMGUI_ALIGN_RIGHT ? 1.0f : 0.5f : 0.0f,
			root->settings.align[AXIS_Y] ? root->settings.align[AXIS_Y] == IMGUI_ALIGN_BOTTOM ? 1.0f : 0.5f : 0.0f,
		};
		child->offset[main] += remaining[main] * scalar[main];
		child->offset[cross] += (remaining[cross] - child->size[cross]) * scalar[cross];

		child_offset_accumulator[main] += child->size[main] + root->settings.child_gap;
		imgui_position_tree(child);
	}
}


IMGUI_Widget *imgui_label(uint64_t id, String8 label) {
	IMGUI_Widget *result = imgui_widget(id);
	result->settings.text = label;
	result->settings.sizing[0] = IMGUI_SIZING_FIXED, result->settings.sizing[1] = IMGUI_SIZING_FIXED;

	Font *font = result->settings.font ? result->settings.font : context->default_font;
	if (font) {
		float2 text_size = measure_text(font, label);
		result->size[0] = text_size.x, result->size[1] = text_size.y;
	}

	return result;
}

IMGUI_Widget *imgui_image(uint64_t id, Image2D *image, float scale) {
	IMGUI_Widget *result = imgui_widget(id);

	result->settings.image = image;
	result->settings.sizing[0] = IMGUI_SIZING_FIXED, result->settings.sizing[1] = IMGUI_SIZING_FIXED;

	if (image) {
		result->size[0] = image->width * scale, result->size[1] = image->height * scale;
	}

	return result;
}

IMGUI_Interact imgui_button_label(String8 label) {
	IMGUI_Widget *box = imgui_widget(hash64(label.text, label.length));
	imgui_parent(imgui_label(hash64_combine(box->id, __LINE__), label), box);
	return imgui_interact(box->id, imgui_rect_cached(box));
}

IMGUI_Interact imgui_button_image(Image2D *image, float scale) {
	IMGUI_Widget *box = imgui_widget(hash64(image, 8));

	IMGUI_Widget *icon = imgui_child(0, box);
	icon->settings.image = image;
	icon->settings.sizing[0] = IMGUI_SIZING_FIXED, icon->settings.sizing[1] = IMGUI_SIZING_FIXED;

	if (image)
		icon->size[0] = image->width * scale, icon->size[1] = image->height * scale;

	return imgui_interact(box->id, imgui_rect_cached(box));
}

IMGUI_Interact imgui_sliderf(uint64_t id, float *t, float min, float max) {
	IMGUI_Widget *track = imgui_widget(id);
	IMGUI_Widget *thumb = imgui_child(hash64_combine(id, __LINE__), track);

	uint32_t flow = IMGUI_HORIZONTAL;

	track->settings.sizing[flow] = IMGUI_SIZING_GROW;
	track->settings.sizing[!flow] = IMGUI_SIZING_FIXED;

	thumb->settings.bg = imgui_peek_style().fg; // colors will also be decided by theme

	thumb->settings.sizing[0] = track->settings.sizing[1];
	thumb->settings.sizing[1] = track->settings.sizing[0];

	track->size[!flow] = context->default_font->bake_size; // will be decided by theme
	thumb->size[flow] = context->default_font->bake_size; // will be decided by theme

	Rectangle track_rect = imgui_rect_cached(track);
	Rectangle thumb_rect = imgui_rect_cached(thumb);

	float track_offset[] = { track_rect.x, track_rect.y };
	float thumb_offset[] = { thumb_rect.x, thumb_rect.y };

	float track_size[] = { track_rect.width, track_rect.height };
	float thumb_size[] = { thumb_rect.width, thumb_rect.height };

	float travel = track_size[flow] - thumb_size[flow];

	IMGUI_Interact interact = imgui_interact(track->id, track_rect);
	if (interact.held) {
		float mouse[] = { context->mouse.position.x, context->mouse.position.y };
		mouse[flow] -= track_offset[flow] + thumb_size[flow] * 0.5f;
		mouse[flow] = clampf(mouse[flow], 0.0f, travel);

		float mouse_ratio = (travel != 0.0f) ? (mouse[flow] / travel) : 0.0f;
		*t = min + (mouse_ratio * (max - min));
	}

	float t_norm = max - min != 0.0f ? (*t - min) / (max - min) : 0.0f;
	thumb->offset[flow] = t_norm * travel;

	return interact;
}

IMGUI_Widget *imgui_spacer(void) {
	IMGUI_Widget *result = imgui_widget_blank(0);

	uint32_t aligned_axis = 0;
	if (context->parent_stack_cursor) {
		IMGUI_Widget *parent = context->parent_stack[context->parent_stack_cursor - 1];
		imgui_parent(result, parent);
		aligned_axis = parent->settings.flow;
	}
	result->settings.sizing[aligned_axis] = IMGUI_SIZING_GROW;

	return result;
}
