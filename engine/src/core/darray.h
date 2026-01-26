#ifndef DARRAY_H_
#define DARRAY_H_

#include "arena.h"
typedef struct {
	size_t count;
	size_t capacity;
} ArrayHeader;

ENGINE_API void *_darray_grow(Arena *arena, void *old_ptr, size_t element_size);

#define _darray_header(a) ((ArrayHeader *)((char *)(a) - sizeof(ArrayHeader)))

#define darray_count(a) ((a) ? _darray_header(a)->count : 0)
#define darray_capacity(a) ((a) ? _darray_header(a)->capacity : 0)

#define darray_push(arena_ptr, a, v) (                                                                                          \
	((a) && _darray_header(a)->count < _darray_header(a)->capacity) ? 0 : ((a) = _darray_grow((arena_ptr), (a), sizeof(*(a)))), \
	(a)[_darray_header(a)->count++] = (v))

#endif /* DARRAY_H_ */
