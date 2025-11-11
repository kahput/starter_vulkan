#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define array_count(array) sizeof(array) / sizeof(*array)

#define max(a, b) (a >= b ? a : b)
#define min(a, b) (a <= b ? a : b)
