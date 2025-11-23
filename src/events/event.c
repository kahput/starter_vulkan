#include "event.h"
#include "core/logger.h"

// typedef struct {
// 	Event events[1024];
// 	uint32_t read_idx;
// 	uint32_t write_idx;
// } EventQueue;

typedef struct event_listener_list {
	PFN_on_event on_event[16];
	uint32_t count;
} EventListenerList;

static EventListenerList layers[MAX_EVENT_TYPES];

bool event_system_startup(void) { return true; }
bool event_system_shutdown(void) { return false; }

bool event_subscribe(uint16_t type_enum, PFN_on_event on_event) {
	if (type_enum == SV_EVENT_NULL || type_enum > MAX_EVENT_TYPES) {
		LOG_WARN("Event: type id[%d] outside valid range, ignoring subscribe request", type_enum);
		return false;
	}
	if (layers[type_enum].count >= 16) {
		LOG_WARN("Event: type id[%d] storage full, ignoring subscribe request", type_enum);
		return false;
	}

	EventListenerList *listeners = &layers[type_enum];
	listeners->on_event[listeners->count++] = on_event;

	return true;
}
bool event_unsubscribe(uint16_t event_type, PFN_on_event on_event) {
	LOG_WARN("Event: function not implemented, ignoring unsubscribe request");
	return false;
}

bool event_emit(Event *event) {
	if (event->header.type == SV_EVENT_NULL || event->header.size >= MAX_EVENT_SIZE) {
		LOG_WARN("Event: type id[%d] outside valid range, ignoring emit request", event->header.type);
		return false;
	}

	EventListenerList *listeners = &layers[event->header.type];

	for (uint32_t index = 0; index < listeners->count; ++index) {
		listeners->on_event[index](event);
	}

	return true;
}
