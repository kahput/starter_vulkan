#pragma once

#include "renderer/renderer_types.h"

bool renderer_begin_frame(void);

bool renderer_submit_mesh(void);
bool renderer_submit_material(void);
bool renderer_submit_draw(void);

bool renderer_end_frame(void);
