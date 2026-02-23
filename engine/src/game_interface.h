#pragma once

#include "assets.h"
#include "event.h"
#include "renderer/backend/vulkan_api.h"
#include "scene.h"

typedef struct {
	void *permanent_memory;
	size_t permanent_memory_size;

	void *transient_memory;
	size_t transient_memory_size;

	VulkanContext *vk_context;
	AssetLibrary *asset_library;
} GameContext;

typedef struct FrameInfo {
	Camera camera;
} FrameInfo;

typedef struct {
	FrameInfo (*on_update)(GameContext *, float dt);
	bool (*on_event)(GameContext *, EventCode code, void *event, void *receiver);
} GameInterface;

typedef GameInterface *(*PFN_game_hookup)(void);

#define GAME_HOOKUP_NAME "game_hookup"
