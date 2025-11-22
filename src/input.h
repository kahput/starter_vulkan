#pragma once

#include "common.h"

bool input_system_startup(void);
bool input_system_shutdown(void);

bool input_system_update(void);

bool input_is_key_pressed(int key);
bool input_is_key_released(int key);

bool input_is_key_down(int key);
bool input_is_key_up(int key);
