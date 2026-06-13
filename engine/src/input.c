#include "input.h"
#include "core/input_types.h"

static InputState *state = NULL;

void input_set_context(InputState *in_state) {
	state = in_state;
}

void input_update(void) {
	for (uint32_t index = 0; index < KEY_CODE_COUNT; ++index) {
		struct Key *key = &state->keys[index];
		key->last = key->state;
	}

	for (uint32_t index = 0; index < MOUSE_BUTTON_COUNT; ++index) {
		struct Button *button = &state->buttons[index];
		button->last = button->state;
	}

	state->motion.last_x = state->motion.x;
	state->motion.last_y = state->motion.y;
}

bool input_key_released(KeyboardKey key) {
	return state->keys[key].state == false && state->keys[key].last == true;
}

bool input_key_pressed(KeyboardKey key) {
	return state->keys[key].state == true && state->keys[key].last == false;
}

bool input_key_down(KeyboardKey key) {
	return state->keys[key].state == true;
}
bool input_key_up(KeyboardKey key) {
	return state->keys[key].state == false;
}

bool input_mouse_pressed(MouseButton button) {
	return state->buttons[button].state == true && state->buttons[button].last == false;
}
bool input_mouse_released(MouseButton button) {
	return state->buttons[button].state == false && state->buttons[button].last == true;
}

bool input_mouse_down(MouseButton button) {
	return state->buttons[button].state == true;
}
bool input_mouse_up(MouseButton button) {
	return state->buttons[button].state == false;
}

double input_mouse_x(void) {
	return state->motion.x;
}
double input_mouse_y(void) {
	return state->motion.y;
}

double2 input_mouse_position(void) {
	return (double2){ state->motion.x, state->motion.y };
}

double input_mouse_dx(void) {
	return state->motion.x - state->motion.last_x;
}
double input_mouse_dy(void) {
	return state->motion.y - state->motion.last_y;
}

double2 input_mouse_delta(void) {
	return (double2){ input_mouse_dx(), input_mouse_dy() };
}

void input_feed_key(KeyboardKey key, bool pressed) {
	state->keys[key].state = pressed;
}

void input_feed_mouse_button(MouseButton button, bool pressed) {
	state->buttons[button].state = pressed;
}

void input_feed_mouse_motion(double x, double y) {
	state->motion.x = x;
	state->motion.y = y;
}
