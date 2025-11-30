#ifndef VK_RESOURCE_POOL_H
#define VK_RESOURCE_POOL_H

#include "vk_types.h"

#include "core/identifiers.h"
#include "core/logger.h"

struct arena *arena;

typedef Handle ResourceHandle;

#define DEFINE_POOL_STRUCT(Name, T)   \
	typedef struct {                  \
		uint16_t generation;          \
		union {                       \
			T resource;               \
			uint32_t next_free_index; \
		} as;                         \
	} Name##Slot;                     \
	typedef struct {                  \
		Name##Slot *slots;            \
		uint32_t count, capacity;     \
		uint32_t free_index;          \
	} Name##Pool

DEFINE_POOL_STRUCT(Buffer, VulkanBuffer);

#endif
