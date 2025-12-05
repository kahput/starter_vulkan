#pragma once

#include "renderer/renderer_types.h"

typedef Handle MeshHandle;
typedef Handle MaterialHandle;

struct arena;
struct platform;

bool renderer_startup(struct arena *arena, struct platform *platform);

bool renderer_begin_frame(void);

bool renderer_submit_mesh(void);
bool renderer_submit_material(void);
bool renderer_submit_draw(void);

bool renderer_end_frame(void);
