#include "common.h"
#include "core/input_types.h"

typedef struct WM_Window WM_Window;
typedef enum {
	WM_EVENT_TYPE_NONE,
	WM_EVENT_TYPE_WINDOW_CLOSE,
	WM_EVENT_TYPE_WINDOW_RESIZE,
	WM_EVENT_TYPE_WINDOW_FOCUS_GAINED,
	WM_EVENT_TYPE_WINDOW_FOCUS_LOST,

	WM_EVENT_TYPE_KEY_PRESS,
	WM_EVENT_TYPE_KEY_RELEASE,

	WM_EVENT_TYPE_MOUSE_MOVE,
	WM_EVENT_TYPE_MOUSE_PRESS,
	WM_EVENT_TYPE_MOUSE_RELEASE,
	WM_EVENT_TYPE_MOUSE_SCROLL,
} WM_EventType;

typedef struct {
	WM_EventType type;
	WM_Window *window;
	union {
		struct {
			uint32_t width;
			uint32_t height;
		} resize;

		struct {
			KeyboardKey key_code;
			bool is_repeat;
		} key;

		struct {
			int32_t x;
			int32_t y;
			int32_t delta_x;
			int32_t delta_y;
		} mouse_move;
		struct {
			MouseButton button;
			int32_t x;
			int32_t y;
		} mouse_button;
		struct {
			float delta_x;
			float delta_y;
		} mouse_scroll;
	} as;
} WM_Event;

bool wm_event_poll(WM_Event *out_event);

WM_Window *wm_window_open(uint32_t width, uint32_t height, const char *title);
void wm_window_close(WM_Window *window);
bool wm_window_is_valid(WM_Window *window);

void wm_window_set_min(WM_Window *window, uint32_t width, uint32_t height);
void wm_window_set_max(WM_Window *window, uint32_t width, uint32_t height);

Rectangle wm_client_rect(WM_Window *window);

void *wm_native_window_handle(WM_Window *window);
