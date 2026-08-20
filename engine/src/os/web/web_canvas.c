#include "core/shape2.h"
#include "os.h"

bool os_display_startup(void);
void os_display_shutdown(void);

OS_Surface *os_surface_open(uint32_t width, uint32_t height, String8 title, OS_SurfaceFlags flags);
OS_Surface *os_surface_open_with_parent(OS_Surface *parent, uint32_t width, uint32_t height, String8 title, OS_SurfaceFlags flags);
void os_surface_close(OS_Surface *surface);

void os_surface_show(OS_Surface *surface);
void os_surface_hide(OS_Surface *surface);

void os_surface_set_min(OS_Surface *surface, uint32_t width, uint32_t height);
void os_surface_set_max(OS_Surface *surface, uint32_t width, uint32_t height);
Rectangle os_client_rect(OS_Surface *surface);

bool os_event_poll(OS_Event *out_event);

void *os_native_surface_handle(OS_Surface *surface);
