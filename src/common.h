#pragma once

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>


#define countof(array) sizeof(array) / sizeof(*array)
#define member_size(type, member) (sizeof(((type *)0)->member))

#define max(a, b) (a > b ? a : b)
#define min(a, b) (a < b ? a : b)

#define clamp(value, minimum, maximum) (min(max(value, minimum), maximum))

#define MAX_FILE_NAME_LENGTH UINT8_MAX
#define MAX_FILE_PATH_LENGTH 512

#define STATIC_ASSERT_(COND, LINE) typedef char static_assertion_##LINE[(!!(COND)) * 2 - 1]
#define STATIC_ASSERT(COND, MSG) STATIC_ASSERT_(COND, MSG)

#define KB(bytes) ((bytes) * 1000ULL)
#define MB(bytes) ((KB(bytes)) * 1000ULL)
#define GB(bytes) ((MB(bytes)) * 1000ULL)

#define KiB(bytes) ((bytes) * 1024ULL)
#define MiB(bytes) ((KiB(bytes)) * 1024ULL)
#define GiB(bytes) ((MiB(bytes)) * 1024ULL)
