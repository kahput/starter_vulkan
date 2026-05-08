#ifndef UI_H_
#define UI_H_

#include <common.h>
#include <core/arena.h>
#include <core/cmath.h>

typedef struct {
	float2 mouse_position;
	bool mouse_down;

	int32_t hot_item;
	int32_t active_item;
} GUI;

GUI g_ui = { 0 };

void ui_begin(void);
void ui_end(void);
bool ui_button(int32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h);

#endif /* UI_H_ */
