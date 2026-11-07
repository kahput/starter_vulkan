#include "input.h"
#include "core/debug.h"
#include "core/input_types.h"
#include "core/logger.h"

static Input *state = NULL;
void input_set_context(Input *in_state) { state = in_state; }

void input_begin_frame(Input *s) {
	state = s;

	for (uint32_t key_code = 0; key_code < KEY_CODE_MAX; ++key_code)
		state->keys_was[key_code] = state->keys_now[key_code];

	for (uint32_t button_index = 0; button_index < MOUSE_BUTTON_MAX; ++button_index)
		state->mouse.buttons_was[button_index] = state->mouse.buttons_was[button_index];
	memory_copy(state->pads_was, state->pads_now, sizeof(state->pads_now));

	state->mouse.dx = 0.0f;
	state->mouse.dy = 0.0f;
}

bool input_key_released(KeyCode key) { return state->keys_now[key] == false && state->keys_was[key] == true; }
bool input_key_pressed(KeyCode key) { return state->keys_now[key] == true && state->keys_was[key] == false; }
bool input_key_held(KeyCode key) { return state->keys_now[key] == true; }

bool input_mouse_pressed(MouseButton button) { return state->mouse.buttons_now[button] == true && state->mouse.buttons_was[button] == false; }
bool input_mouse_released(MouseButton button) { return state->mouse.buttons_now[button] == false && state->mouse.buttons_now[button] == true; }
bool input_mouse_held(MouseButton button) { return state->mouse.buttons_now[button] == true; }

float2 input_mouse_position(void) { return (float2){ state->mouse.x, state->mouse.y }; }
float2 input_mouse_delta(void) { return (float2){ state->mouse.dx, state->mouse.dy }; }

bool input_gamepad_connected(int pad) {
	return (uint32_t)pad < countof(state->pads_now) ? state->pads_now[pad].connected : false;
}

bool input_gamepad_held(int pad, GamepadButton button) {
	return (uint32_t)pad < GAMEPAD_SLOT_MAX ? (state->pads_now[pad].buttons & (1 << button)) : false;
}
bool input_gamepad_pressed(int pad, GamepadButton button) {
	return (uint32_t)pad < GAMEPAD_SLOT_MAX ? ((state->pads_now[pad].buttons & (1 << button)) && (state->pads_was[pad].buttons & (1 << button)) == 0) : false;
}
bool input_gamepad_released(int pad, GamepadButton button) {
	return (uint32_t)pad < GAMEPAD_SLOT_MAX ? ((state->pads_now[pad].buttons & (1 << button)) == 0 && (state->pads_was[pad].buttons & (1 << button))) : false;
}

float input__normalize_axis(int16_t axis) {
	float val = axis / 32767.0f;
	return fabsf(val) < 0.2f ? 0.0f : sign(val) * invlerp(0.2f, 1.0f, fabsf(val));
}

float input_gamepad_axis(int pad, GamepadAxis axis) {
	return (uint32_t)pad < GAMEPAD_SLOT_MAX ? input__normalize_axis(state->pads_now[pad].axes[axis]) : 0.0f;
}
// TODO: Radial deadzone
float2 input_gamepad_vector(int pad, GamepadAxis x, GamepadAxis y) {
	return (float2){ input_gamepad_axis(pad, x), input_gamepad_axis(pad, y) };
}

void input_action_map(Input *input, InputAction *actions, uint32_t action_count) {
	memory_copy(input->actions, actions, MIN(action_count * sizeof(InputAction), sizeof(input->actions)));
}

float input_action_strength(int action) {
	float result = 0.0f;

	for (uint32_t index = 0; index < countof(state->actions[0]); ++index) {
		if (state->actions[action][index].type == INPUT_MAPPING_NONE) continue;

		int code = state->actions[action][index].code;
		float scale = state->actions[action][index].scale;

		switch (state->actions[action][index].type) {
			case INPUT_MAPPING_KEYBOARD_KEY:
				result = (float)input_key_held(code);
				break;
			case INPUT_MAPPING_MOUSE_BUTTON:
				result = (float)input_mouse_held(code);
				break;
			case INPUT_MAPPING_GAMEPAD_BUTTON:
				result = (float)input_gamepad_held(0, code);
				break;
			case INPUT_MAPPING_GAMEPAD_AXIS:
				result = (input_gamepad_axis(0, code) * scale);
				break;

			default:
				break;
		}

		if (result != 0.0f) break;
	}

	return result;
}

// TODO: Radial deadzone?
float2 input_action_vector(int right, int left, int up, int down) {
	return (float2){
		input_action_strength(right) - input_action_strength(left),
		input_action_strength(up) - input_action_strength(down),
	};
}

bool input_action_pressed(int action) {
	bool result = false;

	for (uint32_t index = 0; index < countof(state->actions[0]); ++index) {
		if (state->actions[action][index].type == INPUT_MAPPING_NONE) continue;

		int code = state->actions[action][index].code;
		float scale = state->actions[action][index].scale;

		switch (state->actions[action][index].type) {
			case INPUT_MAPPING_KEYBOARD_KEY:
				result = input_key_pressed(code);
				break;
			case INPUT_MAPPING_MOUSE_BUTTON:
				result = input_mouse_pressed(code);
				break;
			case INPUT_MAPPING_GAMEPAD_BUTTON:
				result = input_gamepad_pressed(0, code);
				break;
			case INPUT_MAPPING_GAMEPAD_AXIS: {
				float now = input__normalize_axis(state->pads_now[0].axes[code]);
				float was = input__normalize_axis(state->pads_was[0].axes[code]);

				result = (now * scale) > 0.0f && (was * scale) <= 0.0f;
			} break;
			default:
				break;
		}

		if (result != false) break;
	}

	return result;
}

bool input_action_released(int action) {
	bool result = false;

	for (uint32_t index = 0; index < countof(state->actions[0]); ++index) {
		if (state->actions[action][index].type == INPUT_MAPPING_NONE) continue;

		int code = state->actions[action][index].code;
		float scale = state->actions[action][index].scale;

		switch (state->actions[action][index].type) {
			case INPUT_MAPPING_KEYBOARD_KEY:
				result = input_key_released(code);
				break;
			case INPUT_MAPPING_MOUSE_BUTTON:
				result = input_mouse_released(code);
				break;
			case INPUT_MAPPING_GAMEPAD_BUTTON:
				result = input_gamepad_released(0, code);
				break;
			case INPUT_MAPPING_GAMEPAD_AXIS: {
				float now = input__normalize_axis(state->pads_now[0].axes[code]);
				float was = input__normalize_axis(state->pads_was[0].axes[code]);

				result = (now * scale) <= 0.0f && (was * scale) > 0.0f;
			} break;
			default:
				break;
		}

		if (result != false) break;
	}

	return result;
}

bool input_action_held(int action) {
	bool result = false;

	for (uint32_t index = 0; index < countof(state->actions[0]); ++index) {
		if (state->actions[action][index].type == INPUT_MAPPING_NONE) continue;

		int code = state->actions[action][index].code;
		float scale = state->actions[action][index].scale;

		switch (state->actions[action][index].type) {
			case INPUT_MAPPING_KEYBOARD_KEY:
				result = input_key_held(code);
				break;
			case INPUT_MAPPING_MOUSE_BUTTON:
				result = input_mouse_held(code);
				break;
			case INPUT_MAPPING_GAMEPAD_BUTTON:
				result = input_gamepad_held(0, code);
				break;
			case INPUT_MAPPING_GAMEPAD_AXIS:
				result = (input_gamepad_axis(0, code) * scale) > 0.0f;
				break;
			default:
				break;
		}

		if (result != false) break;
	}

	return result;
}
