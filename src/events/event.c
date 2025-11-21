#include "event.h"

typedef struct {
	Event events[1024];
	uint32_t read_idx;
	uint32_t write_idx;
} EventQueue;

typedef struct {
	uint32_t event_type;
	PFN_on_event slot;
} EventRegister;

static EventRegister event_pair[1024];
static uint32_t count = 0;

bool event_system_startup(void) { return true; }
bool event_system_shutdown(void) { return false; }

void event_register(uint16_t event_type, PFN_on_event on_event) {
	event_pair[count].slot = on_event;
	event_pair[count++].event_type = event_type;
}
void event_unregister(uint16_t event_type, PFN_on_event on_event) {
}

void event_emit(Event *event) {
	if (event->header.type == 0)
		return;
	for (uint32_t index = 0; index < array_count(event_pair); ++index) {
		if (event_pair[index].event_type == event->header.type)
			event_pair[index].slot(event);
	}
}
