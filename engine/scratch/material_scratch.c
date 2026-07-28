#include "common.h"
#include "core/arena.h"
#include "gfx.h"
#include "gfx/gfx_types.h"
#include "os.h"
#include "core/logger.h"
#include <vulkan/vulkan_core.h>

int main(void) {
	logger_set_level(LOG_LEVEL_DEBUG);

	os_display_startup();
	OS_Surface *window = os_surface_open(1280, 720, s("material_scratch"), OS_SURFACE_FLAG_RESIZEABLE);

	GFX_Device device[] = { 0 };
	gfx_device_make(device);

	GFX_Swapchain *swapchain = gfx_swapchain_make(device, window, "win");
	GFX_Image *white_texture = gfx_image_make(device, 1, 1,
		(ImageOptions){
		  .debug_name = "default:white",
		  .usage = IMAGE_USAGE_TRANSFER | IMAGE_USAGE_SAMPLE,
		  .pixels = (uint8_t[]){ 255, 255, 255, 255 },
		});

	for (bool is_open = true; is_open;) {
		for (OS_Event event; os_event_poll(&event);) {
			switch (event.type) {
				case OS_EVENT_TYPE_SURFACE_CLOSE:
					is_open = false;
					break;
				case OS_EVENT_TYPE_SURFACE_RESIZE:
					gfx_swapchain_resize(device, swapchain, event.as.resize.width, event.as.resize.height);
					break;

				case OS_EVENT_TYPE_KEY_PRESS:
				case OS_EVENT_TYPE_KEY_RELEASE:
					break;
				default:
					break;
			}
		}

		GFX_Command *cmd = gfx_frame_begin(device);
		GFX_Image *screen = gfx_backbuffer(device, cmd, swapchain);
		if (screen) {
			gfx_cmd_image_blit(cmd, (Rectangle){ 0 }, white_texture, (Rectangle){ 0 }, screen);
			gfx_cmd_image_transition(cmd, RESOURCE_USAGE_PRESENT, screen);
		}
		gfx_frame_end(device, cmd);
	}

	gfx_device_destroy(device);
	os_display_shutdown();

	return 0;
}
