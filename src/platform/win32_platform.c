#include "win32_platform.h"
#include "platform.h"

#ifdef PLATFORM_WINDOWS
bool platform_init_win32(Platform *platform) {
	return false;
}


bool win32_platform_startup(Platform *platform) {
	return false;
}
void win32_platform_shutdown(Platform *platform) {
	return false;
}

void win32_platform_poll_events(Platform *platform);
bool win32_platform_should_close(Platform *platform);

void win32_platform_get_window_size(Platform *platform, uint32_t *width, uint32_t *height);
void win32_platform_get_framebuffer_size(Platform *platform, uint32_t *width, uint32_t *height);

struct VkSurfaceKHR_T;
struct VkInstance_T;

bool win32_platform_create_vulkan_surface(Platform *platform, struct VkInstance_T *instance, struct VkSurfaceKHR_T **surface);
#endif
