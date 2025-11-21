#include "input.h"

#include "event.h"
#include "events/input_events.h"

#include "core/input_types.h"
#include "core/logger.h"

bool on_key_press(Event *event);

bool input_system_startup(void) {
	event_register(SV_EVENT_KEY_PRESSED, on_key_press);

	return true;
}
bool input_system_shutdown(void) { return true; }

bool on_key_press(Event *event) {
	KeyEvent *key_event = (KeyEvent *)event;

	if (key_event->key <= 100) {
		LOG_DEBUG("Printable key %c pressed", key_event->key);
	}

	if (key_event->mods & SV_MOD_KEY_SHIFT) {
		LOG_DEBUG("Mod Key SHIFT pressed");
	}

	return true;
}
