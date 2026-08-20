#include "os.h"
#include <bits/time.h>
#include <time.h>
#include <unistd.h>

uint64_t os_time_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

void os_sleep_ms(uint32_t ms) {
	struct timespec ts, remaining;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000;

	while (nanosleep(&ts, &remaining) == -1)
		ts = remaining;
}
