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

#define MAX_FILE_NAME_LENGTH UINT8_MAX
#define MAX_FILE_PATH_LENGTH 512

#define STATIC_ASSERT_(COND, LINE) typedef char static_assertion_##LINE[(!!(COND)) * 2 - 1]
#define STATIC_ASSERT(COND, MSG) STATIC_ASSERT_(COND, MSG)
