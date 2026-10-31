#include "input.h"
#include "core/input_types.h"
#include "core/logger.h"

static InputState *state = NULL;
void input_set_context(InputState *in_state) { state = in_state; }

void input_begin_frame(void) {
	for (uint32_t key_code = 0; key_code < KEY_CODE_MAX; ++key_code) {
		struct Key *key = &state->keys[key_code];
		key->prev = key->curr;
	}

	for (uint32_t button_index = 0; button_index < MOUSE_BUTTON_MAX; ++button_index) {
		struct Button *button = &state->mouse.buttons[button_index];
		button->prev = button->curr;
	}

	state->mouse.dx = 0.0f;
	state->mouse.dy = 0.0f;
}

bool input_key_released(KeyCode key) { return state->keys[key].curr == false && state->keys[key].prev == true; }
bool input_key_pressed(KeyCode key) { return state->keys[key].curr == true && state->keys[key].prev == false; }
bool input_key_down(KeyCode key) { return state->keys[key].curr == true; }

bool input_mouse_pressed(MouseButton button) { return state->mouse.buttons[button].curr == true && state->mouse.buttons[button].prev == false; }
bool input_mouse_released(MouseButton button) { return state->mouse.buttons[button].curr == false && state->mouse.buttons[button].prev == true; }
bool input_mouse_down(MouseButton button) { return state->mouse.buttons[button].curr == true; }
bool input_mouse_up(MouseButton button) { return state->mouse.buttons[button].curr == false; }

float2 input_mouse_position(void) { return (float2){ state->mouse.x, state->mouse.y }; }
float2 input_mouse_delta(void) { return (float2){ state->mouse.dx, state->mouse.dy }; }
