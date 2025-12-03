#include "input.h"

#include "event.h"
#include "events/platform_events.h"

#include "core/logger.h"

static struct {
	struct Key {
		bool state, last;
	} keys[SV_KEY_LAST];

	struct Button {
		bool state, last;
	} buttons[SV_MOUSE_BUTTON_LAST];

	struct {
		double x, y;
		double last_x, last_y;

		bool initialized, virtual_cursor;
	} motion;

} state;

static bool on_key_event(Event *event);
static bool on_mouse_button_event(Event *event);
static bool on_mouse_motion_event(Event *event);

bool input_system_startup(void) {
	event_subscribe(SV_EVENT_KEY_PRESSED, on_key_event);
	event_subscribe(SV_EVENT_KEY_RELEASED, on_key_event);

	event_subscribe(SV_EVENT_MOUSE_MOTION, on_mouse_motion_event);
	event_subscribe(SV_EVENT_MOUSE_BUTTON_PRESSED, on_mouse_button_event);
	event_subscribe(SV_EVENT_MOUSE_BUTTON_RELEASED, on_mouse_button_event);

	return true;
}

bool input_system_shutdown(void) {
	event_unsubscribe(SV_EVENT_KEY_PRESSED, on_key_event);
	event_unsubscribe(SV_EVENT_KEY_RELEASED, on_key_event);

	event_unsubscribe(SV_EVENT_MOUSE_MOTION, on_mouse_motion_event);
	event_unsubscribe(SV_EVENT_MOUSE_BUTTON_PRESSED, on_mouse_button_event);
	event_unsubscribe(SV_EVENT_MOUSE_BUTTON_RELEASED, on_mouse_button_event);

	return true;
}

bool input_system_update(void) {
	for (uint32_t index = 0; index < SV_KEY_LAST; ++index) {
		struct Key *key = &state.keys[index];
		key->last = key->state;
	}
	for (uint32_t index = 0; index < SV_MOUSE_BUTTON_LAST; ++index) {
		struct Button *button = &state.buttons[index];
		button->last = button->state;
	}

	state.motion.last_x = state.motion.x;
	state.motion.last_y = state.motion.y;

	return true;
}

bool input_key_released(int key) {
	return state.keys[key].state == true && state.keys[key].last == false;
}
bool input_key_pressed(int key) {
	return state.keys[key].state == false && state.keys[key].last == true;
}

bool input_key_down(int key) {
	return state.keys[key].state == true;
}
bool input_key_up(int key) {
	return state.keys[key].state == false;
}

bool input_mouse_pressed(int button) {
	return state.buttons[button].state == true && state.buttons[button].last == false;
}
bool input_mouse_released(int button) {
	return state.buttons[button].state == false && state.buttons[button].last == true;
}

bool input_mouse_down(int button) {
	return state.buttons[button].state == true;
}
bool input_mouse_up(int button) {
	return state.buttons[button].state == false;
}

double input_mouse_x(void) {
	return state.motion.x;
}
double input_mouse_y(void) {
	return state.motion.y;
}

double input_mouse_delta_x(void) {
	return state.motion.x - state.motion.last_x;
}
double input_mouse_delta_y(void) {
	return state.motion.y - state.motion.last_y;
}

bool on_key_event(Event *event) {
	KeyEvent *key_event = (KeyEvent *)event;

	if (key_event->leave) {
		for (uint32_t index = 0; index < array_count(state.keys); ++index) {
			state.keys[index].state = false;
			state.keys[index].last = false;
		}

		return true;
	}

	if (key_event->mods & SV_MOD_KEY_SHIFT) {
		LOG_TRACE("Mod Key SHIFT held");
	}

	if (event->header.type == SV_EVENT_KEY_PRESSED) {
		state.keys[key_event->key].state = true;
		if (key_event->key <= 100) {
			LOG_TRACE("Printable key %c pressed", key_event->key);
		}
	} else if (event->header.type == SV_EVENT_KEY_RELEASED) {
		state.keys[key_event->key].state = false;
		if (key_event->key <= 100) {
			LOG_TRACE("Printable key %c released", key_event->key);
		}
	}

	return true;
}

bool on_mouse_button_event(Event *event) {
	MouseButtonEvent *mouse_event = (MouseButtonEvent *)event;

	const char *mouse_button_str[] = {
		[SV_MOUSE_BUTTON_LEFT] = "Left",
		[SV_MOUSE_BUTTON_RIGHT] = "Right",
		[SV_MOUSE_BUTTON_MIDDLE] = "Middle",
		[SV_MOUSE_BUTTON_SIDE] = "Side",
		[SV_MOUSE_BUTTON_EXTRA] = "Extra",
		[SV_MOUSE_BUTTON_FORWARD] = "Forward",
		[SV_MOUSE_BUTTON_BACK] = "Back",
	};

	if (mouse_event->mods & SV_MOD_KEY_SHIFT) {
		LOG_TRACE("Mod Key SHIFT held");
	}

	if (event->header.type == SV_EVENT_MOUSE_BUTTON_PRESSED) {
		state.buttons[mouse_event->button].state = true;
		LOG_TRACE("%s mouse button pressed", mouse_button_str[mouse_event->button]);
	} else if (event->header.type == SV_EVENT_MOUSE_BUTTON_RELEASED) {
		state.buttons[mouse_event->button].state = false;
		LOG_TRACE("%s mouse button released", mouse_button_str[mouse_event->button]);
	}

	return true;
}
bool on_mouse_motion_event(Event *event) {
	MouseMotionEvent *motion_event = (MouseMotionEvent *)event;

	state.motion.x = motion_event->x;
	state.motion.y = motion_event->y;

	if (motion_event->virtual_cursor != state.motion.virtual_cursor) {
		state.motion.virtual_cursor = motion_event->virtual_cursor;

		state.motion.last_x = state.motion.x;
		state.motion.last_y = state.motion.y;
	}

	if (state.motion.initialized == false) {
		state.motion.initialized = true;

		state.motion.last_x = state.motion.x;
		state.motion.last_y = state.motion.y;
	}

	return true;
}
