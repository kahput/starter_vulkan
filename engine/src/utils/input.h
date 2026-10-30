#pragma once

#include "common.h"
#include "core/arena.h"
#include "core/input_types.h"

typedef struct {
	struct Key {
		bool curr, prev;
	} keys[KEY_CODE_MAX];

	struct {
		struct Button {
			bool curr, prev;
		} buttons[MOUSE_BUTTON_MAX];

		float x, y;
		float dx, dy;
	} mouse;
} InputState;

void input_set_context(InputState *state);

void input_begin_frame(void);

bool input_key_pressed(KeyCode key);
bool input_key_released(KeyCode key);
bool input_key_down(KeyCode key);

bool input_mouse_pressed(MouseButton button);
bool input_mouse_released(MouseButton button);
bool input_mouse_down(MouseButton button);
bool input_mouse_up(MouseButton button);

float2 input_mouse_position(void);
float2 input_mouse_delta(void);

/* bool input_gamepad_connected(int pad); */

/* bool input_gamepad_pressed(int pad, GamepadButton button); */
/* bool input_gamepad_released(int pad, GamepadButton button); */
/* bool input_gamepad_down(int pad, GamepadButton button); */

/* float input_gamepad_axis(int pad, GamepadAxis axis); */
/* float2 input_gamepad_vector(int pad, GamepadAxis x, GamepadAxis y); */
