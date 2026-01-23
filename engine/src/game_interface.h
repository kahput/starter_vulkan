#pragma once

#include "assets.h"
#include "common.h"
#include "core/arena.h"
#include "renderer/backend/vulkan_api.h"

typedef struct {
	void* game_memory;
    size_t game_memory_size;

	VulkanContext *vk_context;
	AssetLibrary *asset_library;
} GameContext;

typedef struct {
	bool (*on_load)(GameContext *);
	bool (*on_update)(GameContext *, float dt);
	bool (*on_unload)(GameContext *);
} GameInterface;

typedef GameInterface *(*PFN_game_hookup)(void);

#define GAME_HOOKUP_NAME "game_hookup"
