#pragma once

#include "event.h"


typedef struct {
	EventHeader header;

	uint32_t key, action, mods;
} KeyEvent;

EVENT_DEFINE(KeyEvent);

// typedef struct {
// 	EventHeader header;
//
// 	uint32_t
// }
