// Kawaii Craft - VOPI Engine
// SPDX-License-Identifier: Proprietary
// Copyright (C) 2026 VOPI Team

#pragma once

#if IS_VOPI_ENGINE

#include <string>

namespace video
{
	class IVideoDriver;
	class ITexture;
}

struct ItemMesh;

/*
	Renders a generic-node ItemMesh once into a shared offscreen target,
	applies a screen-space outline (alpha dilate, driven by the
	inventory_mesh_outline settings) plus an edge bleed for clean bilinear
	filtering, and uploads the result as a regular cached texture named
	`texture_name`.

	Returns nullptr when baking is unavailable (no render-to-texture
	support, texture creation or readback failure) — the caller then falls
	back to direct 3D mesh rendering.

	Must be called from the render thread between beginScene/endScene.
*/
video::ITexture *bakeItemIcon(video::IVideoDriver *driver,
		ItemMesh *imesh, const std::string &texture_name);

#endif
