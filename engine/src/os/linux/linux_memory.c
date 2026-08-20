#include "os.h"
#include <sys/mman.h>
#include "core/debug.h"

#define flag_match(flags, flag) ((flags & (flag)) == (flag))

void *os_memory_reserve(size_t size) {
	void *result = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ASSERT(result != MAP_FAILED);

	return result;
}

void os_memory_commit(void *ptr, size_t size) {
	mprotect(ptr, size, PROT_READ | PROT_WRITE);
}

void os_memory_decommit(void *ptr, size_t size) {
	madvise(ptr, size, MADV_DONTNEED);
	mprotect(ptr, size, PROT_NONE);
}
void os_memory_release(void *ptr, size_t size) {
	munmap(ptr, size);
}
