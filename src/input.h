#pragma once

#include "common.h"

bool input_system_startup(void);
bool input_system_shutdown(void);

bool input_system_update(void);

bool input_key_pressed(int key);
bool input_key_released(int key);

bool input_key_down(int key);
bool input_key_up(int key);

bool input_mouse_pressed(int button);
bool input_mouse_released(int button);

bool input_mouse_down(int button);
bool input_mouse_up(int button);

double input_mouse_x(void);
double input_mouse_y(void);

double input_mouse_delta_x(void);
double input_mouse_delta_y(void);
