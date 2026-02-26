#pragma once

#include "common.h"

void *slist_create(void *array, size_t stride, uint32_t capacity);

void *slist_pop_(void **first_free);
void slist_push(void **first_free, void *slot);

#define slist_pop(first_free) (slist_pop_((void **)first_free))
#define slist_push(first_free, slot, type) ((type *)slist_pop_((void **)first_free))
