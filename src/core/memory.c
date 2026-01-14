#include "memory.h"

#include <string.h>

void memory_zero(void *pointer, size_t size) {
	memset(pointer, 0, size);
}
