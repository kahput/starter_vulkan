#include "common.h"
#include "core/input_types.h"
#include "core/os.h"
#include <core.h>
#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <linux/input.h>
#include <stdio.h>

#define INPUT_DIRECTORY "/dev/input"

bool bitset_has(uint8_t *bitset, uint32_t bit) {
	return bitset[bit / 8] & (1 << (bit % 8));
}

static const string8 gamepad_button_to_display_string[GAMEPAD_BUTTON_MAX] = {
	[GAMEPAD_BUTTON_SELECT] = comp8("Select"),
	[GAMEPAD_BUTTON_GUIDE] = comp8("Guide"),
	[GAMEPAD_BUTTON_START] = comp8("Start"),
	[GAMEPAD_BUTTON_SOUTH] = comp8("South"),
	[GAMEPAD_BUTTON_EAST] = comp8("East"),
	[GAMEPAD_BUTTON_WEST] = comp8("West"),
	[GAMEPAD_BUTTON_NORTH] = comp8("North"),
	[GAMEPAD_BUTTON_DOWN] = comp8("Down"),
	[GAMEPAD_BUTTON_RIGHT] = comp8("Right"),
	[GAMEPAD_BUTTON_UP] = comp8("Up"),
	[GAMEPAD_BUTTON_LEFT] = comp8("Left"),
	[GAMEPAD_BUTTON_LSTICK] = comp8("L3"),
	[GAMEPAD_BUTTON_RSTICK] = comp8("R3"),
	[GAMEPAD_BUTTON_LSHOULDER] = comp8("LT"),
	[GAMEPAD_BUTTON_RSHOULDER] = comp8("RT"),
};

static const string8 gamepad_axes_to_display_string[GAMEPAD_BUTTON_MAX] = {
	[GAMEPAD_AXIS_LX] = comp8("LX"),
	[GAMEPAD_AXIS_LY] = comp8("LY"),
	[GAMEPAD_AXIS_RX] = comp8("RX"),
	[GAMEPAD_AXIS_RY] = comp8("RY"),
	[GAMEPAD_AXIS_LT] = comp8("LT"),
	[GAMEPAD_AXIS_RT] = comp8("RT"),
};

typedef struct {
	GamepadButton buttons[(BTN_THUMBR + 1) - BTN_MISC];
	GamepadAxis axes[ABS_HAT3Y + 1];
	bool flip_axis[ABS_HAT3Y + 1];
} GP_Mapping;

GP_Mapping mapping_8bitdo_ultimate_2c = {
	.buttons = {
	  [BTN_SOUTH - BTN_MISC] = GAMEPAD_BUTTON_SOUTH,
	  [BTN_EAST - BTN_MISC] = GAMEPAD_BUTTON_EAST,
	  [BTN_WEST - BTN_MISC] = GAMEPAD_BUTTON_NORTH,
	  [BTN_NORTH - BTN_MISC] = GAMEPAD_BUTTON_WEST,
	  [BTN_TL - BTN_MISC] = GAMEPAD_BUTTON_LSHOULDER,
	  [BTN_TR - BTN_MISC] = GAMEPAD_BUTTON_RSHOULDER,
	  [BTN_SELECT - BTN_MISC] = GAMEPAD_BUTTON_SELECT,
	  [BTN_START - BTN_MISC] = GAMEPAD_BUTTON_START,
	  [BTN_MODE - BTN_MISC] = GAMEPAD_BUTTON_GUIDE,
	  [BTN_THUMBL - BTN_MISC] = GAMEPAD_BUTTON_LSTICK,
	  [BTN_THUMBR - BTN_MISC] = GAMEPAD_BUTTON_RSTICK,
	},
	.axes = {
	  [ABS_X] = GAMEPAD_AXIS_LX,
	  [ABS_Y] = GAMEPAD_AXIS_LY,
	  [ABS_Z] = GAMEPAD_AXIS_LT,
	  [ABS_RX] = GAMEPAD_AXIS_RX,
	  [ABS_RY] = GAMEPAD_AXIS_RY,
	  [ABS_RZ] = GAMEPAD_AXIS_RT,
	},
	.flip_axis = {
	  [ABS_Y] = true,
	  [ABS_RY] = true,
	  [ABS_HAT0Y] = true,
	  [ABS_HAT1Y] = true,
	  [ABS_HAT2Y] = true,
	  [ABS_HAT3Y] = true,
	},
};

typedef struct {
	int fd;
	char path[PATH_MAX];
	char name[NAME_MAX];

	struct input_absinfo info[ABS_HAT3Y + 1];
} GP_Joystick;

typedef struct {
	bool connected;
	uint32_t buttons;
	int16_t axes[GAMEPAD_AXIS_MAX];
} Gamepad;

struct {
	int inotify;
	GP_Joystick joysticks[8];
} gp_state[1] = { { -1, { { 0 } } } };

void js_close(GP_Joystick *js) {
	if (js->fd >= 0) {
		close(js->fd);
		printf("'%s' Disconnected\n", js->name);
	}
	js->fd = -1;
	js->path[0] = '\0';
	js->name[0] = '\0';
}

static bool js_open(const char *path) {
	GP_Joystick *js = 0;
	uint64_t pathlen = strlen(path);
	for (uint32_t jid = 0; jid < countof(gp_state->joysticks); ++jid) {
		if (memory_equals(gp_state->joysticks[jid].path, path, MIN(pathlen, PATH_MAX))) return false;
		if (gp_state->joysticks[jid].fd < 0) { js = gp_state->joysticks + jid; }
	}
	if (js == 0) return false;
	snprintf(js->path, sizeof(js->path), "%s", path);

	int fd = open(js->path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		js_close(js);
		return false;
	}

	uint8_t evbits[(EV_CNT + 7) / 8] = { 0 };
	uint8_t keybits[(KEY_CNT + 7) / 8] = { 0 };
	uint8_t absbits[(ABS_CNT + 7) / 8] = { 0 };
	struct input_id id;

	if (
		ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0 ||
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0 ||
		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0 ||
		ioctl(fd, EVIOCGID, &id) < 0 //
	) {
		fprintf(stderr, "Failed to query input device: %s\n", strerror(errno));
		js_close(js);
		return false;
	}

	for (uint32_t axis = ABS_X; axis < ABS_HAT3Y + 1; ++axis) {
		if (bitset_has(absbits, axis) == false) continue;
		ioctl(fd, EVIOCGABS(axis), &js->info[axis]);
	}

	bool ok =
		bitset_has(evbits, EV_ABS) &&
		bitset_has(absbits, ABS_X) &&
		bitset_has(absbits, ABS_Y) &&
		bitset_has(keybits, BTN_GAMEPAD);
	if (ok == false) {
		js_close(js);
		return false;
	}

	if (js == 0) {
		js_close(js);
		return false;
	}

	js->fd = fd;
	ioctl(fd, EVIOCGNAME(sizeof(js->name)), js->name);

	printf("'%s' Connected\n", js->name);
	return true;
}

int main(void) {
	string8 input_path = s("/dev/input/");

	int inotify = inotify_init1(O_NONBLOCK | O_CLOEXEC);
	if (inotify >= 0)
		inotify_add_watch(inotify, (char *)input_path.bytes, IN_CREATE | IN_ATTRIB | IN_DELETE);

	for (uint32_t jid = 0; jid < countof(gp_state->joysticks); ++jid)
		gp_state->joysticks[jid].fd = -1;

	OS_Directory dir = os_opendir(input_path);
	for (OS_DirectoryEntry entry; os_readdir(dir, &entry);) {
		if (str8_contains(entry.name, s("event")) == false) continue;

		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/%.*s", INPUT_DIRECTORY, arg8(entry.name));

		js_open(path);
	}

	Gamepad pads[8] = { 0 };

	uint64_t start_time = os_time_ns();
	while (true) {
		if (inotify >= 0) {
			// NOTE: Limits to 64 events per frame
			char buffer[(sizeof(struct inotify_event) + NAME_MAX + 1) * 64];

			ssize_t offset = 0;
			ssize_t bytes = read(inotify, buffer, sizeof(buffer));
			while (offset < bytes) {
				struct inotify_event *ev = (struct inotify_event *)(buffer + offset);
				offset += sizeof(struct inotify_event) + ev->len;

				string8 name = ev->len ? str8z(ev->name) : s("");
				if (name.length == 0 || str8_contains(name, s("event")) == false) continue;

				char path[PATH_MAX];
				uint64_t pathlen = snprintf(path, sizeof(path), "%s/%.*s", INPUT_DIRECTORY, arg8(name));

				if (ev->mask & (IN_CREATE | IN_ATTRIB))
					js_open(path);
				else if (ev->mask & IN_DELETE) {
					for (uint32_t jid = 0; jid < countof(gp_state->joysticks); jid++) {
						GP_Joystick *js = gp_state->joysticks + jid;
						if (js->fd >= 0 && memory_equals(js->path, path, MIN(PATH_MAX, pathlen))) {
							js_close(js);
							break;
						}
					}
				}
			}
		}

		for (uint32_t jid = 0; jid < countof(gp_state->joysticks); ++jid) {
			GP_Joystick *js = gp_state->joysticks + jid;
			Gamepad *pad = pads + jid;

			if (js->fd < 0) {
				memory_zero(pad, sizeof(pad[0]));
				continue;
			}
			pad->connected = true;

			struct input_event events[64];
			ssize_t bytes = read(js->fd, events, sizeof(events));
			if (bytes < 0) {
				if (errno == EINTR) continue;
				if (errno == EAGAIN) continue;
				js_close(js);
				memory_zero(pad, sizeof(pad[0]));
				continue;
			}

			uint32_t event_count = bytes / sizeof(events[0]);
			for (uint32_t event_index = 0; event_index < event_count; ++event_index) {
				struct input_event *ev = events + event_index;

				switch (ev->type) {
					case EV_SYN: // TODO: Handle this
						break;
					case EV_ABS: {
						if (ev->code >= ABS_HAT0X && ev->code <= ABS_HAT3Y) {
							bool flip = mapping_8bitdo_ultimate_2c.flip_axis[ev->code];
							int32_t value = flip ? -ev->value : ev->value;

							// NOTE: All hats map to a single dpad
							bool x = ev->code % 2 == 0;

							GamepadButton negative = x ? GAMEPAD_BUTTON_LEFT : GAMEPAD_BUTTON_DOWN;
							GamepadButton positive = x ? GAMEPAD_BUTTON_RIGHT : GAMEPAD_BUTTON_UP;

							pad->buttons &= ~(1 << negative | 1 << positive);
							if (value != 0)
								pad->buttons |= 1 << (value > 0 ? positive : negative);
						} else if (ev->code < ABS_HAT0X) {
							bool flip = mapping_8bitdo_ultimate_2c.flip_axis[ev->code];
							GamepadAxis axis = mapping_8bitdo_ultimate_2c.axes[ev->code];
							struct input_absinfo *info = js->info + ev->code;
							ASSERT(info->minimum < info->maximum - 1 && "?");

							int64_t min = info->minimum, max = info->maximum;
							int64_t range = max - min, halfrange = range / 2;

							int64_t val = ev->value - min; // 0 -> range
							if (axis == GAMEPAD_AXIS_LT || axis == GAMEPAD_AXIS_RT)
								val = clamp1i(val * 32767LL / range, 0, 32767);
							else
								val = clamp1i((val - halfrange) * 32767LL / halfrange, -32767, 32767);

							pad->axes[axis] = flip ? -val : val;
						}
					} break;
					case EV_KEY:
						if (ev->code >= BTN_MISC && ev->code <= BTN_THUMBR) {
							GamepadButton button = mapping_8bitdo_ultimate_2c.buttons[ev->code - BTN_MISC];
							if (ev->value == 0)
								pad->buttons &= ~(1 << button);
							else
								pad->buttons |= 1 << button;
						}
						break;

					default:
						ASSERT(false && "Unhandled event type for gamepad");
				}
			}
		}

		Gamepad *pad = 0;
		for (uint32_t pid = 0; pid < 8; ++pid)
			pad = pads[pid].connected ? pads + pid : 0;
		if (pad) {
			for (GamepadButton button = 0; button < GAMEPAD_BUTTON_MAX; ++button) {
				if (pad->buttons & (1 << button))
					LOG_INFO("%.*s down.", arg8(gamepad_button_to_display_string[button]));
			}

			if (abs1i(pad->axes[0]) >= 128 || abs1i(pad->axes[1]) >= 128) LOG_INFO("LStick { %d, %d }", pad->axes[0], pad->axes[1]);
			if (abs1i(pad->axes[2]) >= 128 || abs1i(pad->axes[3]) >= 128) LOG_INFO("RStick { %d, %d }", pad->axes[2], pad->axes[3]);
			if (abs1i(pad->axes[4]) >= 128) LOG_INFO("LT %d", pad->axes[4]);
			if (abs1i(pad->axes[5]) >= 128) LOG_INFO("RT %d", pad->axes[5]);
		}
	}

	return 0;
}
