#include "renderer.h"

bool renderer_begin_frame(void) {

	return true;
}

bool renderer_submit_mesh(void);
bool renderer_submit_material(void);
bool renderer_submit_draw(void);

bool renderer_end_frame(void);
