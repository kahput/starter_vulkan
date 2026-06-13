#include "core/arena.h"
#include "gfx.h"
#include "os.h"
#include "core/logger.h"

int main(void) {
	LOG_INFO("hello world!");

	os_display_startup();
	OS_Surface *window = os_surface_open(10, 10, s("material_scratch"), OS_SURFACE_FLAG_RESIZEABLE);

	bool is_open = true;
	while (is_open) {
		OS_Event event = { 0 };
		while (os_event_poll(&event)) {
			switch (event.type) {
				case OS_EVENT_TYPE_SURFACE_CLOSE:
					is_open = false;
					break;
				case OS_EVENT_TYPE_KEY_PRESS:
				case OS_EVENT_TYPE_KEY_RELEASE:
					break;
				default:
					break;
			}
		}
	}

	os_display_shutdown();

	return 0;
}
