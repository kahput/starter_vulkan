#include "input.h"

#include "event.h"
#include "events/input_events.h"

#include "core/input_types.h"
#include "core/logger.h"

static struct {
	struct Keys {
		bool current, last;
	} keys[SV_KEY_LAST];
} input_state;

static bool on_key_event(Event *event);
static bool on_mouse_button_event(Event *event);
static bool on_mouse_motion_event(Event *event);

bool input_system_startup(void) {
	event_register(SV_EVENT_KEY_PRESSED, on_key_event);
	event_register(SV_EVENT_KEY_RELEASED, on_key_event);

	event_register(SV_EVENT_MOUSE_MOTION, on_mouse_motion_event);
	event_register(SV_EVENT_MOUSE_BUTTON_PRESSED, on_mouse_button_event);
	event_register(SV_EVENT_MOUSE_BUTTON_RELEASED, on_mouse_button_event);

	return true;
}
bool input_system_shutdown(void) {
	event_unregister(SV_EVENT_KEY_PRESSED, on_key_event);
	event_unregister(SV_EVENT_KEY_RELEASED, on_key_event);

	event_unregister(SV_EVENT_MOUSE_MOTION, on_mouse_motion_event);
	event_unregister(SV_EVENT_MOUSE_BUTTON_PRESSED, on_mouse_button_event);
	event_unregister(SV_EVENT_MOUSE_BUTTON_RELEASED, on_mouse_button_event);

	return true;
}

bool input_system_update(void) {
	for (uint32_t index = 0; index < SV_KEY_LAST; ++index) {
		struct Keys *key = &input_state.keys[index];
		key->last = key->current;
	};

	return true;
}

bool input_is_key_pressed(int key) {
	return input_state.keys[key].current == true && input_state.keys[key].last == false;
}
bool input_is_key_released(int key) {
	return input_state.keys[key].current == false && input_state.keys[key].last == true;
}

bool input_is_key_down(int key) {
	return input_state.keys[key].current;
}
bool input_is_key_up(int key) {
	return input_state.keys[key].current;
}

bool on_key_event(Event *event) {
	KeyEvent *key_event = (KeyEvent *)event;

	if (key_event->mods & SV_MOD_KEY_SHIFT) {
		LOG_DEBUG("Mod Key SHIFT held");
	}

	if (event->header.type == SV_EVENT_KEY_PRESSED) {
		if (key_event->key <= 100) {
			input_state.keys[key_event->key].current = true;
			LOG_DEBUG("Printable key %c pressed", key_event->key);
		}
	} else if (event->header.type == SV_EVENT_KEY_RELEASED)
		if (key_event->key <= 100) {
			input_state.keys[key_event->key].current = false;
			LOG_DEBUG("Printable key %c released", key_event->key);
		}

	return true;
}
bool on_key_release(Event *event) {
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
		LOG_DEBUG("Mod Key SHIFT held");
	}

	if (event->header.type == SV_EVENT_MOUSE_BUTTON_PRESSED) {
		LOG_DEBUG("%s mouse button pressed", mouse_button_str[mouse_event->button]);
	} else if (event->header.type == SV_EVENT_MOUSE_BUTTON_RELEASED)
		LOG_DEBUG("%s mouse button released", mouse_button_str[mouse_event->button]);

	return true;
}
bool on_mouse_motion_event(Event *event) {
	MouseMotionEvent *motion_event = (MouseMotionEvent *)event;

	LOG_DEBUG("MouseMotion { x = %d, y = %d }", motion_event->x, motion_event->y);
	return true;
}
