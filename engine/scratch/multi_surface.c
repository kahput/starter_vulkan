#include "common.h"
#include "core/arena.h"
#include "core/logger.h"
#include "os.h"
#include <core.h>

#include "gfx.h"
#include "gfx/gfx_types.h"

int main(void) {
    logger_set_level(LOG_LEVEL_DEBUG);
	Arena arena[] = { arena_make(GiB(1)) };

	os_display_startup();
	GFX_Device device[] = { 0 };
	if (gfx_device_make(device) == false) return -1;

#define SURFACE_COUNT 3
	OS_Surface *surfaces[SURFACE_COUNT] = { 0 };
	GFX_Swapchain *swapchains[SURFACE_COUNT] = { 0 };
	for (uint32_t index = 0; index < SURFACE_COUNT; ++index) {
		String8 name = str8_pushf(arena, s("surface%d"), index);
		surfaces[index] = os_surface_open(1280, 720, name, OS_SURFACE_FLAG_RESIZEABLE);
		swapchains[index] = gfx_swapchain_make(device, surfaces[index], (char *)name.text);
	}

	for (bool is_open = true; is_open;) {

		uint2 resize[SURFACE_COUNT] = { 0 };
		for (OS_Event ev; os_event_poll(&ev);) {
			if (ev.type == OS_EVENT_TYPE_SURFACE_CLOSE)
				is_open = false;
			else if (ev.type == OS_EVENT_TYPE_SURFACE_RESIZE) {
				int32_t found_index = -1;
				for (uint32_t index = 0; index < SURFACE_COUNT; ++index) {
					if (surfaces[index] == ev.surface) {
						found_index = index;
						break;
					}
				}

				if (found_index != -1)
					resize[found_index] = *(uint2 *)&ev.as.resize;
			}
		}

		for (uint32_t index = 0; index < SURFACE_COUNT; ++index) {
			if (resize[index].x != 0 && resize[index].y)
				gfx_swapchain_resize(device, swapchains[index], resize[index].x, resize[index].y);
		}

		GFX_Command *cmd = gfx_frame_begin(device);
		if (cmd == 0) break;

		for (uint32_t index = 0; index < SURFACE_COUNT; ++index) {
			GFX_Image *backbuffer = gfx_swapchain_backbuffer(device, cmd, swapchains[index]);
			if (backbuffer) {
				gfx_cmd_image_clear(cmd, (Rectangle){ 0 }, WHITE, backbuffer);
			}
		}

		gfx_frame_end(device, cmd);
	}

	gfx_device_destroy(device);
	os_display_shutdown();

	return 0;
}
