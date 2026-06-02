// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 VOPI Team

#pragma once

#include "irrlichttypes_bloated.h"
#include <IGUIElement.h>
#include <IGUIEnvironment.h>
#include <string>
#include <vector>

namespace video {
	class IVideoDriver;
	class ITexture;
}

class Client;

// Formspec element that renders a top-down map of the world around the player.
//
// Self-contained renderer: reads voxel data straight from the client map
// (ClientMap), derives a representative colour per surface column, bakes its
// own texture and draws it into the element rect, with relief shading, a player
// marker and optional custom points. It does NOT use the engine HUD minimap —
// the map has its own center, zoom and lifecycle.
//
// Phase 1: shows the currently loaded area around the player (no fog-of-war /
// persistence yet — those build on top of this renderer later).
//
// (VOPI Engine extension.)
class GUIMapElement : public gui::IGUIElement
{
public:
	struct MapPoint
	{
		v3f world_pos;
		video::SColor color;
		// Optional icon texture name. When set (and loadable), the icon is
		// drawn at the point; otherwise a colour square is drawn as fallback.
		std::string icon;
	};

	GUIMapElement(gui::IGUIEnvironment *env, gui::IGUIElement *parent, s32 id,
			const core::rect<s32> &rectangle, Client *client);
	virtual ~GUIMapElement();

	void setPoints(std::vector<MapPoint> points) { m_points = std::move(points); }

	// Zoom: how many world nodes the (square) map window spans across its full
	// width. Smaller = closer in. Clamped to a sane range by the setter.
	void setViewNodes(s32 nodes);

	// Focus: world point the map is centered on. Without a focus the map follows
	// the local player. setFocus pins it to an arbitrary point (e.g. a POI).
	void setFocus(v3f focus) { m_focus = focus; m_has_focus = true; }
	void clearFocus() { m_has_focus = false; }

	// Static icon for the player marker. Empty keeps the default white dot.
	void setPlayerIcon(std::string icon) { m_player_icon = std::move(icon); }

	// Edge length (pixels) for every map icon — POI icons and the player icon.
	// 0 keeps the responsive default (a fraction of the map width). The formspec
	// gives it in coordinate units; parseMap converts to pixels.
	void setIconSize(s32 size_px) { m_icon_size = size_px; }

	virtual void draw() override;
	virtual bool OnEvent(const SEvent &event) override;

private:
	// Rebuild the baked map texture centered on `center` (node coords).
	void rebuildTexture(v3s16 center);
	// Draw the player marker and the custom points on top of the baked texture,
	// given the on-screen rect and the current node-coord center.
	void drawMarkers(const core::rect<s32> &rect, v3s16 center);

	Client *m_client;
	video::IVideoDriver *m_driver;

	std::vector<MapPoint> m_points;

	// View span in world nodes across the window (zoom). Default 256 (1 node
	// per texture pixel). 128 = 2x zoom-in (node = 2x2 px, nearest upscale).
	s32 m_view_nodes = 256;

	// Optional pinned focus center (world coords). When unset, follow player.
	v3f m_focus;
	bool m_has_focus = false;

	// Static player-marker icon (empty => default white dot) and the shared icon
	// edge length in pixels for POI + player icons (0 => responsive default).
	std::string m_player_icon;
	s32 m_icon_size = 0;

	// Baked top-down texture of the surrounding terrain.
	video::ITexture *m_texture = nullptr;
	v3s16 m_cached_center;          // node-coord center the texture was baked at
	s32 m_cached_view_nodes = 0;    // view span the texture was baked at
	u64 m_last_update_ms = 0;       // throttle timestamp
	bool m_has_texture = false;
};
