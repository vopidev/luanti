// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 cx384

#include "item_visuals_manager.h"

#include "wieldmesh.h"
#include "client.h"
#include "texturesource.h"
#include "itemdef.h"
#include "inventory.h"
#include <IMesh.h>
#if IS_VOPI_ENGINE
#include "icon_baker.h"
#include "renderingengine.h"
#include "settings.h"
#include "porting.h"
#include "log.h"
#include <IVideoDriver.h>
#endif

struct ItemVisualsManager::ItemVisuals
{
	ItemMesh item_mesh;
	Palette *palette;

	AnimationInfo inventory_normal;
	AnimationInfo inventory_overlay;

	// ItemVisuals owns the frames and AnimationInfo points to them
	std::vector<FrameSpec> frames_normal;
	std::vector<FrameSpec> frames_overlay;

#if IS_VOPI_ENGINE
	// Pre-rendered outlined icon (icon_baker); nullptr when not baked
	video::ITexture *baked_icon = nullptr;
	// Already submitted to (or rejected by) the background bake queue
	bool bake_requested = false;
#endif

	ItemVisuals() :
		palette(nullptr)
	{}

	~ItemVisuals()
	{
		if (item_mesh.mesh)
			item_mesh.mesh->drop();
#if IS_VOPI_ENGINE
		if (baked_icon)
			RenderingEngine::get_video_driver()->removeTexture(baked_icon);
#endif
	}

	DISABLE_CLASS_COPY(ItemVisuals);
};

ItemVisualsManager::ItemVisuals *ItemVisualsManager::createItemVisuals( const ItemStack &item,
		Client *client) const
{
	// This is not thread-safe
	sanity_check(std::this_thread::get_id() == m_main_thread);

	IItemDefManager *idef = client->idef();

	const ItemDefinition &def = item.getDefinition(idef);
	ItemImageDef inventory_image = item.getInventoryImage(idef);
	ItemImageDef inventory_overlay = item.getInventoryOverlay(idef);

	// Key only consists of item name + image name,
	// because animation currently cannot be overridden by meta
	std::string cache_key = def.name;
	if (!inventory_image.name.empty())
		cache_key.append("/").append(inventory_image.name);
	if (!inventory_overlay.name.empty())
		cache_key.append(":").append(inventory_overlay.name);


	// Skip if already in cache
	auto it = m_cached_item_visuals.find(cache_key);
	if (it != m_cached_item_visuals.end())
		return it->second.get();

	infostream << "Lazily creating item texture and mesh for \""
			<< cache_key << "\"" << std::endl;

	ITextureSource *tsrc = client->getTextureSource();

	auto iv = std::make_unique<ItemVisuals>();

	// Create inventory image textures
	int frame_length = 0;
	iv->frames_normal = createAnimationFrames(tsrc, inventory_image.name,
			inventory_image.animation, frame_length);
	iv->inventory_normal = AnimationInfo(&iv->frames_normal, frame_length);

	// Create inventory overlay textures
	iv->frames_overlay = createAnimationFrames(tsrc, inventory_overlay.name,
			inventory_overlay.animation, frame_length);
	iv->inventory_overlay = AnimationInfo(&iv->frames_overlay, frame_length);

	createItemMesh(client, def,
			iv->inventory_normal,
			iv->inventory_overlay,
			&(iv->item_mesh));

	iv->palette = tsrc->getPalette(def.palette_image);

	// Put in cache
	ItemVisuals *ptr = iv.get();
	m_cached_item_visuals[cache_key] = std::move(iv);
	return ptr;
}

// Needed because `ItemVisuals` is not known in the header.
ItemVisualsManager::ItemVisualsManager()
{
	m_main_thread = std::this_thread::get_id();
}

ItemVisualsManager::~ItemVisualsManager()
{
}

void ItemVisualsManager::clear()
{
	m_cached_item_visuals.clear();
#if IS_VOPI_ENGINE
	m_bake_queue.clear();
	m_bake_queue_pos = 0;
#endif
}


video::ITexture *ItemVisualsManager::getInventoryTexture(const ItemStack &item,
		Client *client) const
{
	ItemVisuals *iv = createItemVisuals(item, client);
	if (!iv)
		return nullptr;

	// Texture animation update (if >1 frame)
	return iv->inventory_normal.getTexture(client->getAnimationTime());
}

video::ITexture *ItemVisualsManager::getInventoryOverlayTexture(const ItemStack &item,
		Client *client) const
{
	ItemVisuals *iv = createItemVisuals(item, client);
	if (!iv)
		return nullptr;

	// Texture animation update (if >1 frame)
	return iv->inventory_overlay.getTexture(client->getAnimationTime());
}

ItemMesh *ItemVisualsManager::getItemMesh(const ItemStack &item, Client *client) const
{
	ItemVisuals *iv = createItemVisuals(item, client);
	return iv ? &(iv->item_mesh) : nullptr;
}

#if IS_VOPI_ENGINE
video::ITexture *ItemVisualsManager::getBakedIcon(const ItemStack &item,
		Client *client) const
{
	// Never bakes synchronously: the first sight of an item enqueues a
	// background bake request and the slot keeps the direct mesh render
	// until processBakeQueue() delivers the texture a few frames later.
	// This keeps the pipeline-stalling GPU readback of a bake off the
	// first-scroll path of large inventories, and only items the player
	// actually sees ever consume icon memory.
	ItemVisuals *iv = createItemVisuals(item, client);
	if (!iv)
		return nullptr;

	if (!iv->baked_icon && !iv->bake_requested) {
		iv->bake_requested = true;
		if (bakeQualifies(iv) && g_settings->getBool("inventory_icon_bake"))
			m_bake_queue.push_back(item.name);
	}
	return iv->baked_icon;
}

bool ItemVisualsManager::bakeQualifies(const ItemVisuals *iv) const
{
	// Only generic-node 3D icons. Nodes with animated tiles are baked
	// too — icons deliberately show a static first frame (the baker
	// forces frame 0), trading the live animation for the outline.
	return iv->item_mesh.mesh && iv->item_mesh.needs_shading;
}

void ItemVisualsManager::processBakeQueue(Client *client, float budget_ms) const
{
	if (m_bake_queue_pos >= m_bake_queue.size())
		return;

	const u64 t0 = porting::getTimeUs();
	const u64 budget_us = (u64)(budget_ms * 1000.0f);
	u32 processed = 0;

	while (m_bake_queue_pos < m_bake_queue.size()) {
		if (processed && porting::getTimeUs() - t0 >= budget_us)
			break;
		const std::string name = m_bake_queue[m_bake_queue_pos++];
		ItemStack stack(name, 1, 0, client->idef());
		ItemVisuals *iv = createItemVisuals(stack, client);
		if (iv && bakeQualifies(iv) && !iv->baked_icon) {
			iv->baked_icon = bakeItemIcon(RenderingEngine::get_video_driver(),
					&iv->item_mesh, "__baked_icon:" + name);
		}
		processed++;
	}

	// Fully processed: reclaim the request list
	if (m_bake_queue_pos >= m_bake_queue.size()) {
		m_bake_queue.clear();
		m_bake_queue_pos = 0;
	}
}
#endif

AnimationInfo *ItemVisualsManager::getInventoryAnimation(const ItemStack &item,
		Client *client) const
{
	ItemVisuals *iv = createItemVisuals(item, client);
	if (!iv || iv->inventory_normal.getFrameCount() <= 1)
		return nullptr;
	return &iv->inventory_normal;
}

// Get item inventory overlay animation
// returns nullptr if it is not animated
AnimationInfo *ItemVisualsManager::getInventoryOverlayAnimation(const ItemStack &item,
		Client *client) const
{
	ItemVisuals *iv = createItemVisuals(item, client);
	if (!iv || iv->inventory_overlay.getFrameCount() <= 1)
		return nullptr;
	return &iv->inventory_overlay;
}

Palette* ItemVisualsManager::getPalette(const ItemStack &item, Client *client) const
{
	ItemVisuals *iv = createItemVisuals(item, client);
	if (!iv)
		return nullptr;
	return iv->palette;
}

video::SColor ItemVisualsManager::getItemstackColor(const ItemStack &stack,
	Client *client) const
{
	// Look for direct color definition
	const std::string &colorstring = stack.metadata.getString("color", 0);
	video::SColor directcolor;
	if (!colorstring.empty() && parseColorString(colorstring, directcolor, true))
		return directcolor;
	// See if there is a palette
	Palette *palette = getPalette(stack, client);
	const std::string &index = stack.metadata.getString("palette_index", 0);
	if (palette && !index.empty())
		return (*palette)[mystoi(index, 0, 255)];
	// Fallback color
	return client->idef()->get(stack.name).color;
}

