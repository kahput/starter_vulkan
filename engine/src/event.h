#pragma once
#include "common.h"
#include "core/arena.h"

typedef uint32_t EventCode;

#define EVENT_ID(subsystem, event) (((subsystem) << 24) | (event))
#define EVENT_SUBSYSTEM(code) ((code) >> 24)
#define EVENT_TYPE(code) ((code) & 0x00FFFFFF)

typedef enum {
	EVENT_SUBSYSTEM_CORE = 0x01,
	EVENT_SUBSYSTEM_PLATFORM,
	EVENT_SUBSYSTEM_GAME,
	EVENT_SUBSYSTEM_MAX,
} EventSubsystem;

enum {
	EVENT_CORE_NULL = 0,
	EVENT_CORE_QUIT = EVENT_ID(EVENT_SUBSYSTEM_CORE, 0x01),
};

#define MAX_EVENT_SIZE 128
#define EVENT_STRUCT_DECLARE(name, type)                            \
	STATIC_ASSERT(sizeof(type) <= MAX_EVENT_SIZE);                    \
	static inline void event_##name##_emit(EventCode code, type *data) {    \
		event_emit(code, data, sizeof(type));                         \
	}                                                                 \
	static inline type *event_##name##_push(EventCode code) {               \
		return (type *)event_push(code, sizeof(type), alignof(type)); \
	}

typedef struct EventState EventState;

EventState *event_system_startup(Arena *arena);
bool event_system_shutdown(void);
bool event_system_hookup(EventState *state);
bool event_system_update(void);

typedef bool (*PFN_event_handler)(EventCode code, void *event, void *receiver);

bool event_subscribe(EventCode code, PFN_event_handler on_event, void *receiver);
bool event_unsubscribe(EventCode code, PFN_event_handler on_event, void *receiver);
bool event_subscribe_multi(EventCode *codes, uint32_t count, PFN_event_handler on_event, void *receiver);
#define event_subscribe_list(handler, receiver, ...)                           \
	do {                                                                       \
		EventCode _codes[] = { __VA_ARGS__ };                                  \
		event_subscribe_multi(_codes, countof(_codes), (handler), (receiver)); \
	} while (0)

bool event_emit(EventCode code, void *event, size_t size);
void *event_push(EventCode code, size_t size, size_t alignment);
void event_flush(void);

#define event_emit_struct(code, ptr) event_emit((code), (ptr), sizeof(*(ptr)))
#define event_emit_code(code) event_emit((code), NULL, 0)

#define event_push_struct(code, type) ((type *)event_push((code), sizeof(type), alignof(type)))
#define event_push_code(code) event_push((code), 0, 1)
