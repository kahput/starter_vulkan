#include "ui.h"
#include "commands.h"
#include "common.h"
#include "core/cmath.h"

UIContext *g_ctx = NULL;

UIWidget *widget_peek(void) {
	return &g_ctx->widgets[g_ctx->depth_parent[g_ctx->current_depth]];
}

uint32_t widget_peek_index(void) {
	return g_ctx->depth_parent[g_ctx->current_depth];
}

void ui_frame_begin(UIContext *context) {
	g_ctx = context;
	g_ctx->widget_count = 1;
}

void ui_frame_end(DrawlistBuffer *buffer) {
	// Position
	for (uint32_t index = 1; index < g_ctx->widget_count; ++index) {
		UIWidget *widget = &g_ctx->widgets[index + 1];
		if (widget->parent == 0)
			continue;

		UIWidget *parent = &g_ctx->widgets[widget->parent];

		widget->offset[AXIS2_X] += parent->offset[AXIS2_X] + parent->padding[AXIS2_X][0] + parent->child_offset_accumulator[AXIS2_X];
		widget->offset[AXIS2_Y] += parent->offset[AXIS2_Y] + parent->padding[AXIS2_Y][1] + parent->child_offset_accumulator[AXIS2_Y];

		parent->child_offset_accumulator[parent->orientation] += widget->size[parent->orientation] + parent->child_gap;
	}

	// Draw
	for (uint32_t index = 1; index < g_ctx->widget_count; ++index) {
		UIWidget *widget = &g_ctx->widgets[index];

		widget->rect = (Rectangle){
			widget->offset[AXIS2_X], widget->offset[AXIS2_Y], widget->size[AXIS2_X], widget->size[AXIS2_Y]
		};
		drawlist_push_rect(buffer, widget->rect, widget->background_color);

		g_ctx->cached_widgets[index].id = widget->id;
		g_ctx->cached_widgets[index].rect = widget->rect;
	}

	g_ctx->cached_widget_count = 0;
	for (uint32_t index = 0; index < g_ctx->widget_count; ++index) {
		UIWidget *widget = &g_ctx->widgets[index];
		g_ctx->cached_widgets[g_ctx->cached_widget_count].id = widget->id;
		g_ctx->cached_widgets[g_ctx->cached_widget_count].rect = widget->rect;

		g_ctx->cached_widget_count++;
	}

	memory_zero_array(g_ctx->widgets);
	g_ctx = NULL;
}

UIWidget *widget_push(uint32_t id) {
	uint32_t current_index = g_ctx->widget_count++;
	UIWidget *widget = &g_ctx->widgets[current_index];
	widget->id = id;

	if (g_ctx->current_depth) {
		UIWidget *parent = widget_peek();

		parent->children[parent->children_count++] = current_index;
		widget->parent = widget_peek_index();
	}

	g_ctx->depth_parent[++g_ctx->current_depth] = current_index;

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
	g_ctx->current_depth--;

	// Fit
	if (widget->semantic_size[AXIS2_X].type != UI_SIZE_FIT && widget->semantic_size[AXIS2_Y].type != UI_SIZE_FIT)
		return;

	bool fit[AXIS2_MAX] = {
		widget->semantic_size[AXIS2_X].type == UI_SIZE_FIT,
		widget->semantic_size[AXIS2_Y].type == UI_SIZE_FIT
	};

	uint32_t padding[AXIS2_MAX] = {
		widget->padding[AXIS2_X][0] + widget->padding[AXIS2_X][1],
		widget->padding[AXIS2_Y][0] + widget->padding[AXIS2_Y][1]
	};
	uint32_t child_gap = ((widget->children_count - 1) * widget->child_gap);

	uint32_t main = widget->orientation;
	uint32_t cross = !main;

	widget->size[main] += (padding[main] + child_gap) * fit[main];

	for (uint32_t index = 0; index < widget->children_count; ++index) {
		UIWidget *child = &g_ctx->widgets[widget->children[index]];

		widget->size[main] += child->size[AXIS2_X];

		if (widget->semantic_size[cross].type == UI_SIZE_FIT)
			widget->size[cross] = MAX(widget->size[cross], child->size[cross]);
	}

	widget->size[cross] += (padding[cross]) * fit[cross];
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
	g_ctx->current_depth--;
}

void ui_background_color(Color color) {
	UIWidget *widget = widget_peek();

	widget->background_color = color;
}

void ui_absolute_position(float2 pos) {
	UIWidget *widget = widget_peek();

	widget->offset[AXIS2_X] = pos.x;
	widget->offset[AXIS2_Y] = pos.y;
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
