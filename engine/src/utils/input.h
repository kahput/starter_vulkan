#pragma once

#include "common.h"
#include "core/arena.h"
#include "core/input_types.h"

typedef enum {
	INPUT_MAPPING_NONE,

	INPUT_MAPPING_KEYBOARD_KEY,
	INPUT_MAPPING_MOUSE_BUTTON,
	INPUT_MAPPING_GAMEPAD_BUTTON,
	INPUT_MAPPING_GAMEPAD_AXIS,
} InputMappingType;

typedef struct InputAction {
	InputMappingType type;
	int code; // KeyCode key_code; MouseButton mouse_button; GamepadButton gamepad_button; GamepadAxis gamepad_axis;
	float scale;
} InputAction[4];

#define INPUT_ACTION_MAPPING_MAX 32
typedef struct {
	bool keys_now[KEY_CODE_MAX];
	bool keys_was[KEY_CODE_MAX];

	struct {
		bool buttons_now[MOUSE_BUTTON_MAX];
		bool buttons_was[MOUSE_BUTTON_MAX];

		float x, y;
		float dx, dy;
	} mouse;

	Gamepad pads_now[GAMEPAD_SLOT_MAX];
	Gamepad pads_was[GAMEPAD_SLOT_MAX];

	InputAction actions[INPUT_ACTION_MAPPING_MAX];
	uint32_t action_count;
} Input;

void input_action_map(Input *input, InputAction *actions, uint32_t action_count);
void input_begin_frame(Input *s);

bool input_key_pressed(KeyCode key);
bool input_key_released(KeyCode key);
bool input_key_held(KeyCode key);

bool input_mouse_pressed(MouseButton button);
bool input_mouse_released(MouseButton button);
bool input_mouse_held(MouseButton button);

float2 input_mouse_position(void);
float2 input_mouse_delta(void);

bool input_gamepad_connected(int pad);

bool input_gamepad_pressed(int pad, GamepadButton button);
bool input_gamepad_released(int pad, GamepadButton button);
bool input_gamepad_held(int pad, GamepadButton button);

float input_gamepad_axis(int pad, GamepadAxis axis);
float2 input_gamepad_vector(int pad, GamepadAxis x, GamepadAxis y);

bool input_action_pressed(int action);
bool input_action_released(int action);
bool input_action_held(int action);

float input_action_strength(int action);
float2 input_action_vector(int right, int left, int up, int down);
