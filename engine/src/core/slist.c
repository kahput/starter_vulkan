#include "slist.h"

typedef struct slist_node {
	struct slist_node *next;
} SListNode;

void *slist_create(void *array, size_t stride, uint32_t capacity) {
	for (uint32_t index = 0; index < capacity; ++index) {
		SListNode *element = (SListNode *)((uint8_t *)array + stride * index);
		element->next = (SListNode *)((uint8_t *)element + stride);

		if (index + 1 == capacity)
			element->next = NULL;
	}

	return array;
}

void *slist_pop_(void **first_free) {
	if (first_free == NULL || *first_free == NULL)
		return NULL;

	SListNode *element = (SListNode *)*first_free;
	*first_free = element->next;

	return element;
}

void slist_push_(void **first_free, void *slot) {
	if (first_free == NULL || *first_free == NULL)
		return;

	SListNode *head = *first_free;
	SListNode *element = slot;

	element->next = head;
	*first_free = element;
}
