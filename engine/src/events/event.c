#include "event.h"
#include "core/arena.h"
#include "core/logger.h"

// typedef struct {
// 	Event events[1024];
// 	uint32_t read_idx;
// 	uint32_t write_idx;
// } EventQueue;

#define MAX_LISTENER_PER_EVENT_CODE 32
#define MAX_EVENT_CODES_PER_SUBSYSTEM 32

typedef struct EventListener {
	PFN_event_handler handler;
	void *receiver;
} EventListener;

typedef struct event_listener_list {
	EventListener listeners[MAX_LISTENER_PER_EVENT_CODE];
	uint32_t count;
} EventListenerList;

struct EventState {
	EventListenerList subsystems[EVENT_SUBSYSTEM_MAX * MAX_EVENT_CODES_PER_SUBSYSTEM];
};

static EventState *state;

EventState *event_system_startup(Arena *arena) {
	state = arena_push_struct(arena, EventState);

	return state;
}

bool event_system_shutdown(void) { return false; }

bool event_system_hookup(EventState *state_ptr) {
	state = state_ptr;

	return true;
}

bool event_subscribe(EventCode code, PFN_event_handler on_event, void *receiver) {
	uint8_t subsystem_id = EVENT_SUBSYSTEM(code);
	if (code == EVENT_CORE_NULL || subsystem_id > EVENT_SUBSYSTEM_MAX)
		return false;

	if (state->subsystems[subsystem_id].count >= MAX_LISTENER_PER_EVENT_CODE) {
		LOG_WARN("Event: type id[%d] storage full, aborting %s", code, __func__);
		return false;
	}

	uint32_t listener_list_index = (subsystem_id * MAX_EVENT_CODES_PER_SUBSYSTEM) + EVENT_TYPE(code);
	EventListenerList *subsystem = &state->subsystems[listener_list_index];

	subsystem->listeners[subsystem->count++] = (EventListener){ .receiver = receiver, .handler = on_event };

	return true;
}
bool event_unsubscribe(EventCode code, PFN_event_handler on_event, void *receiver) {
	uint8_t subsystem_id = EVENT_SUBSYSTEM(code);
	if (code == EVENT_CORE_NULL || subsystem_id > EVENT_SUBSYSTEM_MAX)
		return false;

	if (state->subsystems[subsystem_id].count >= MAX_LISTENER_PER_EVENT_CODE) {
		LOG_WARN("Event: type id[%d] storage full, aborting %s", code, __func__);
		return false;
	}

	uint32_t listener_list_index = (subsystem_id * MAX_EVENT_CODES_PER_SUBSYSTEM) + EVENT_TYPE(code);
	EventListenerList *subsystem = &state->subsystems[listener_list_index];

	for (uint32_t receiver_index = 0; receiver_index < subsystem->count; ++receiver_index) {
		EventListener *listener = &subsystem->listeners[receiver_index];

		if (listener->receiver == receiver && listener->handler == on_event) {
			*listener = subsystem->listeners[subsystem->count++ - 1];
			break;
		}
	}

	LOG_WARN("Event: function not implemented, aborting %s", __func__);
	return false;
}

bool event_unsubscribe_all(PFN_event_handler fn, void *receiver) {
	return false;
}

bool event_subscribe_multi(EventCode *codes, uint32_t count, PFN_event_handler fn, void *receiver) {
	for (uint32_t index = 0; index < count; ++index)
		event_subscribe(codes[index], fn, receiver);

	return true;
}

bool event_emit(EventCode code, void *event, size_t size) {
	uint8_t subsystem_id = EVENT_SUBSYSTEM(code);
	if (code == EVENT_CORE_NULL || subsystem_id > EVENT_SUBSYSTEM_MAX)
		return false;

	if (state->subsystems[subsystem_id].count >= MAX_LISTENER_PER_EVENT_CODE) {
		LOG_WARN("Event: type id[%d] storage full, aborting %s", code, __func__);
		return false;
	}

	uint32_t listner_list_index = (subsystem_id * MAX_EVENT_CODES_PER_SUBSYSTEM) + EVENT_TYPE(code);

	EventListenerList *subsystem = &state->subsystems[listner_list_index];
	for (uint32_t index = 0; index < subsystem->count; ++index) {
		EventListener *listener = &subsystem->listeners[index];
		listener->handler(code, event, listener->receiver);
	}

	return true;
}
