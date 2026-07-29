#pragma once

#include "common.h"
#include "core/arena.h"
#include "core/input_types.h"

typedef struct {
	struct Key {
		bool state, last;
	} keys[KEY_CODE_COUNT];

	struct Button {
		bool state, last;
	} buttons[MOUSE_BUTTON_COUNT];

	struct {
		double x, y;
		double last_x, last_y;
	} motion;
} InputState;

void input_update(void);
void input_set_context(InputState *state);

void input_feed_key(KeyboardKey key, bool pressed);
void input_feed_mouse_button(MouseButton button, bool pressed);
void input_feed_mouse_motion(double x, double y);

bool input_key_pressed(KeyboardKey key);
bool input_key_released(KeyboardKey key);
bool input_key_down(KeyboardKey key);
bool input_key_up(KeyboardKey key);

bool input_mouse_pressed(MouseButton button);
bool input_mouse_released(MouseButton button);
bool input_mouse_down(MouseButton button);
bool input_mouse_up(MouseButton button);

double input_mouse_x(void);
double input_mouse_y(void);
double2 input_mouse_position(void);
double input_mouse_dx(void);
double input_mouse_dy(void);
double2 input_mouse_delta(void);
