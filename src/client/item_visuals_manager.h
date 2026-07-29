// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 cx384

#pragma once

#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#if IS_VOPI_ENGINE
#include "irrlichttypes.h"
#endif

struct AnimationInfo;
class Client;
struct ItemStack;
struct ItemMesh;
namespace video { class ITexture; class SColor; }
typedef std::vector<video::SColor> Palette; // copied from src/client/texturesource.h

// Caches data needed to draw an itemstack

struct ItemVisualsManager
{
	ItemVisualsManager();
	~ItemVisualsManager();

	/// Clears the cached visuals
	void clear();

	// Get item inventory texture
	video::ITexture* getInventoryTexture(const ItemStack &item, Client *client) const;

	// Get item inventory overlay texture
	video::ITexture* getInventoryOverlayTexture(const ItemStack &item, Client *client) const;

	// Get item inventory animation
	// returns nullptr if it is not animated
	AnimationInfo *getInventoryAnimation(const ItemStack &item, Client *client) const;

	// Get item inventory overlay animation
	// returns nullptr if it is not animated
	AnimationInfo *getInventoryOverlayAnimation(const ItemStack &item, Client *client) const;

	// Get item mesh
	ItemMesh *getItemMesh(const ItemStack &item, Client *client) const;

#if IS_VOPI_ENGINE
	// Baked (pre-rendered, outlined) icon texture for generic-node 3D
	// icons; nullptr when the item is not baked (2D image, baking
	// disabled or not yet processed by the bake queue). Animated tiles
	// are baked as a static first frame. The first call for an eligible
	// item enqueues a background bake request; the actual bake happens
	// in processBakeQueue().
	video::ITexture *getBakedIcon(const ItemStack &item, Client *client) const;

	// Bakes a few pending on-demand requests per client step within the
	// given time budget, keeping the pipeline-stalling GPU readback of
	// each bake off the first-scroll path of large inventories.
	void processBakeQueue(Client *client, float budget_ms) const;
#endif

	// Get item palette
	Palette* getPalette(const ItemStack &item, Client *client) const;

	// Returns the base color of an item stack: the color of all
	// tiles that do not define their own color.
	video::SColor getItemstackColor(const ItemStack &stack, Client *client) const;

private:
	struct ItemVisuals;

	// The id of the thread that is allowed to use irrlicht directly
	std::thread::id m_main_thread;
	// Cached textures and meshes
	mutable std::unordered_map<std::string, std::unique_ptr<ItemVisuals>> m_cached_item_visuals;

	ItemVisuals* createItemVisuals(const ItemStack &item, Client *client) const;

#if IS_VOPI_ENGINE
	// True when iv qualifies for baking (generic-node mesh, no animated
	// tiles).
	bool bakeQualifies(const ItemVisuals *iv) const;

	// Pending on-demand bake requests (item names)
	mutable std::vector<std::string> m_bake_queue;
	mutable size_t m_bake_queue_pos = 0;
#endif
};
