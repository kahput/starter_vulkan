#pragma once

#include "core/arena.h"
#include "input/input_types.h"

typedef struct InputState InputState;

InputState *input_system_startup(Arena *arena);
bool input_system_shutdown(void);

ENGINE_API bool input_system_hookup(InputState *state_ptr);
ENGINE_API bool input_system_update(void);

ENGINE_API bool input_key_pressed(int key);
ENGINE_API bool input_key_released(int key);

ENGINE_API bool input_key_down(int key);
ENGINE_API bool input_key_up(int key);

ENGINE_API bool input_mouse_pressed(int button);
ENGINE_API bool input_mouse_released(int button);

ENGINE_API bool input_mouse_down(int button);
ENGINE_API bool input_mouse_up(int button);

ENGINE_API double input_mouse_x(void);
ENGINE_API double input_mouse_y(void);

ENGINE_API double input_mouse_dx(void);
ENGINE_API double input_mouse_dy(void);
