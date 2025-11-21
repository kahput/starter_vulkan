#pragma once

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define array_count(array) sizeof(array) / sizeof(*array)
#define member_size(type, member) (sizeof(((type *)0)->member))

#define max(a, b) (a >= b ? a : b)
#define min(a, b) (a <= b ? a : b)

#define MAX_FILE_NAME_LENGTH 512
#define MAX_FILE_PATH_LENGTH 2048

#define STATIC_ASSERT(COND, MSG) typedef char static_assertion[(!!(COND)) * 2 - 1]

typedef struct file {
	uint32_t width, height, channels;

	const char *path;
} File;
