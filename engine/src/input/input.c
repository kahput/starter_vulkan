#include "input.h"

#include "core/arena.h"
#include "event.h"
#include "events/platform_events.h"

#include "core/logger.h"

struct InputState {
	struct Key {
		bool state, last;
	} keys[KEY_CODE_LAST];

	struct Button {
		bool state, last;
	} buttons[MOUSE_BUTTON_LAST];

	struct {
		double x, y;
		double last_x, last_y;

		bool initialized, virtual_cursor;
	} motion;
};

static InputState *state = NULL;

static bool on_key_event(EventCode code, void *event, void *receiver);
static bool on_mouse_button_event(EventCode code, void *event, void *receiver);
static bool on_mouse_motion_event(EventCode code, void *event, void *receiver);

InputState *input_system_startup(Arena *arena) {
	state = arena_push_struct_zero(arena, InputState);

	event_subscribe_list(
		on_key_event, state,
		EVENT_PLATFORM_KEY_PRESSED, EVENT_PLATFORM_KEY_RELEASED);
	event_subscribe(
		EVENT_PLATFORM_MOUSE_MOTION,
		on_mouse_motion_event, state);
	event_subscribe_list(
		on_mouse_button_event, state,
		EVENT_PLATFORM_MOUSE_BUTTON_PRESSED,
		EVENT_PLATFORM_MOUSE_BUTTON_RELEASED);

	return state;
}

bool input_system_shutdown(void) {
	event_unsubscribe(EVENT_PLATFORM_KEY_PRESSED, on_key_event, state);
	event_unsubscribe(EVENT_PLATFORM_KEY_RELEASED, on_key_event, state);

	event_unsubscribe(EVENT_PLATFORM_MOUSE_MOTION, on_mouse_motion_event, state);
	event_unsubscribe(EVENT_PLATFORM_MOUSE_BUTTON_RELEASED, on_mouse_button_event, state);
	event_unsubscribe(EVENT_PLATFORM_MOUSE_BUTTON_RELEASED, on_mouse_button_event, state);

	return true;
}

bool input_system_hookup(InputState *state_ptr) {
	state = state_ptr;

	return true;
}

bool input_system_update(void) {
	for (uint32_t index = 0; index < KEY_CODE_LAST; ++index) {
		struct Key *key = &state->keys[index];
		key->last = key->state;
	}
	for (uint32_t index = 0; index < MOUSE_BUTTON_LAST; ++index) {
		struct Button *button = &state->buttons[index];
		button->last = button->state;
	}

	state->motion.last_x = state->motion.x;
	state->motion.last_y = state->motion.y;

	return true;
}

bool input_key_released(int key) {
	return state->keys[key].state == true && state->keys[key].last == false;
}
bool input_key_pressed(int key) {
	return state->keys[key].state == true && state->keys[key].last == false;
}

bool input_key_down(int key) {
	return state->keys[key].state == true;
}
bool input_key_up(int key) {
	return state->keys[key].state == false;
}

bool input_mouse_pressed(int button) {
	return state->buttons[button].state == true && state->buttons[button].last == false;
}
bool input_mouse_released(int button) {
	return state->buttons[button].state == false && state->buttons[button].last == true;
}

bool input_mouse_down(int button) {
	return state->buttons[button].state == true;
}
bool input_mouse_up(int button) {
	return state->buttons[button].state == false;
}

double input_mouse_x(void) {
	return state->motion.x;
}
double input_mouse_y(void) {
	return state->motion.y;
}

double input_mouse_dx(void) {
	return state->motion.x - state->motion.last_x;
}
double input_mouse_dy(void) {
	return state->motion.y - state->motion.last_y;
}

bool on_key_event(EventCode code, void *event, void *receiver) {
	KeyEvent *key_event = (KeyEvent *)event;
	InputState *input_state = receiver;

	if (key_event->leave) {
		for (uint32_t index = 0; index < countof(input_state->keys); ++index) {
			input_state->keys[index].state = false;
			input_state->keys[index].last = false;
		}

		return true;
	}

	if (key_event->mods & MOD_KEY_SHIFT) {
		LOG_TRACE("Mod Key SHIFT held");
	}

	if (code == EVENT_PLATFORM_KEY_PRESSED) {
		input_state->keys[key_event->key].state = true;
		if (key_event->key <= 100) {
			LOG_TRACE("Printable key %c pressed", key_event->key);
		}
	} else if (code == EVENT_PLATFORM_KEY_RELEASED) {
		input_state->keys[key_event->key].state = false;
		if (key_event->key <= 100) {
			LOG_TRACE("Printable key %c released", key_event->key);
		}
	}

	return true;
}

bool on_mouse_button_event(EventCode code, void *event, void *receiver) {
	MouseButtonEvent *mouse_event = (MouseButtonEvent *)event;
	InputState *input_state = receiver;

	const char *mouse_button_str[] = {
		[MOUSE_BUTTON_LEFT] = "Left",
		[MOUSE_BUTTON_RIGHT] = "Right",
		[MOUSE_BUTTON_MIDDLE] = "Middle",
		[MOUSE_BUTTON_SIDE] = "Side",
		[MOUSE_BUTTON_EXTRA] = "Extra",
		[MOUSE_BUTTON_FORWARD] = "Forward",
		[MOUSE_BUTTON_BACK] = "Back",
	};

	if (mouse_event->mods & MOD_KEY_SHIFT) {
		LOG_TRACE("Mod Key SHIFT held");
	}

	if (code == EVENT_PLATFORM_MOUSE_BUTTON_PRESSED) {
		input_state->buttons[mouse_event->button].state = true;
		LOG_TRACE("%s mouse button pressed", mouse_button_str[mouse_event->button]);
	} else if (code == EVENT_PLATFORM_MOUSE_BUTTON_RELEASED) {
		input_state->buttons[mouse_event->button].state = false;
		LOG_TRACE("%s mouse button released", mouse_button_str[mouse_event->button]);
	}

	return true;
}
bool on_mouse_motion_event(EventCode code, void *event, void *receiver) {
	MouseMotionEvent *motion_event = (MouseMotionEvent *)event;
	InputState *input_state = receiver;

	input_state->motion.x = motion_event->x;
	input_state->motion.y = motion_event->y;

	if (motion_event->virtual_cursor != input_state->motion.virtual_cursor) {
		input_state->motion.virtual_cursor = motion_event->virtual_cursor;

		input_state->motion.last_x = input_state->motion.x;
		input_state->motion.last_y = input_state->motion.y;
	}

	if (input_state->motion.initialized == false) {
		input_state->motion.initialized = true;

		input_state->motion.last_x = input_state->motion.x;
		input_state->motion.last_y = input_state->motion.y;
	}

	return true;
}
