#include "wm.h"

#ifdef LINUX_BUILD
	#include "linux/xcb.c"
	#include "linux/evdev.c"
#elif WEB_BUILD
	#include "web/canvas.c"
#elif WINDOWS_BUILD
	#include "windows/win32.c"
#endif
