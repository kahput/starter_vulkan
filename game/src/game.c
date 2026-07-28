#include <input.h>

#include <common.h>
#include <core/arena.h>
#include <core/logger.h>

#define LINUX_BUILD
#include <os.h>

typedef struct {
	OS_Surface *surface;

	bool initialized;
} State;

State *state = 0;

bool tick(Arena *memory, InputState *input) {
	state = (State *)memory->base;
	input_set_context(input);

	if (state->initialized == false) {
		state->surface = os_surface_open(1280, 720, s("game"), 0);
		state->initialized = true;
	}

	return true;
}
