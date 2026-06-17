// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 VOPI Team

#pragma once

#if IS_VOPI_ENGINE

#include <string>
#include <IGUIEnvironment.h>
#include <IGUIImage.h>
#include <IVideoDriver.h>

// 9-slice background helper: manages 9 GUI image elements (corners, edges, center)
// that together form a scalable rounded background.
//
// Two lifecycle patterns:
//   managed=true  (GameUI)     - images live in GUI tree, auto-drawn, cleanup via setVisible
//   managed=false (FormSpec)   - images are grab()'d, explicit draw(), cleanup via remove()+drop()
struct NineSliceBackground {
	gui::IGUIImage *up_left = nullptr;
	gui::IGUIImage *up = nullptr;
	gui::IGUIImage *up_right = nullptr;
	gui::IGUIImage *left = nullptr;
	gui::IGUIImage *center = nullptr;
	gui::IGUIImage *right = nullptr;
	gui::IGUIImage *down_left = nullptr;
	gui::IGUIImage *down = nullptr;
	gui::IGUIImage *down_right = nullptr;
	bool m_managed = true;

	// Create 9 images, load textures by convention: {prefix}_{position}.png
	// managed=true: images stay in GUI tree (auto-drawn by parent traversal)
	// managed=false: images are grab()'d for manual lifecycle
	inline void init(gui::IGUIEnvironment *env, video::IVideoDriver *driver,
			gui::IGUIElement *parent, const std::string &textures_path,
			const std::string &texture_prefix, bool managed = true)
	{
		m_managed = managed;

		gui::IGUIImage *parts[] = {
			nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr,
			nullptr, nullptr, nullptr
		};
		static const char *suffixes[] = {
			"up_left", "up", "up_right",
			"left", "center", "right",
			"down_left", "down", "down_right"
		};

		for (int i = 0; i < 9; i++) {
			parts[i] = env->addImage(core::rect<s32>(0, 0, 1, 1), parent, -1, nullptr, true);
			std::string path = textures_path + texture_prefix + "_" + suffixes[i] + ".png";
			parts[i]->setImage(driver->getTexture(path.c_str()));
			parts[i]->setScaleImage(true);
			parts[i]->setVisible(false);
			if (!managed)
				parts[i]->grab();
		}

		up_left   = parts[0]; up      = parts[1]; up_right   = parts[2];
		left      = parts[3]; center  = parts[4]; right      = parts[5];
		down_left = parts[6]; down    = parts[7]; down_right = parts[8];
	}

	// Position 9 elements within a bounding rect with given corner size
	inline void setPosition(const core::rect<s32> &rect, s32 corner_size)
	{
		if (!isInitialized())
			return;
		s32 x1 = rect.UpperLeftCorner.X;
		s32 y1 = rect.UpperLeftCorner.Y;
		s32 x2 = rect.LowerRightCorner.X;
		s32 y2 = rect.LowerRightCorner.Y;
		s32 cs = corner_size;

		// Corners
		up_left->setRelativePosition(core::rect<s32>(x1, y1, x1 + cs, y1 + cs));
		up_right->setRelativePosition(core::rect<s32>(x2 - cs, y1, x2, y1 + cs));
		down_left->setRelativePosition(core::rect<s32>(x1, y2 - cs, x1 + cs, y2));
		down_right->setRelativePosition(core::rect<s32>(x2 - cs, y2 - cs, x2, y2));
		// Edges
		up->setRelativePosition(core::rect<s32>(x1 + cs, y1, x2 - cs, y1 + cs));
		down->setRelativePosition(core::rect<s32>(x1 + cs, y2 - cs, x2 - cs, y2));
		left->setRelativePosition(core::rect<s32>(x1, y1 + cs, x1 + cs, y2 - cs));
		right->setRelativePosition(core::rect<s32>(x2 - cs, y1 + cs, x2, y2 - cs));
		// Center
		center->setRelativePosition(core::rect<s32>(x1 + cs, y1 + cs, x2 - cs, y2 - cs));
	}

	// Toggle visibility of all 9 elements
	inline void setVisible(bool visible)
	{
		if (!isInitialized())
			return;
		up_left->setVisible(visible);   up->setVisible(visible);   up_right->setVisible(visible);
		left->setVisible(visible);      center->setVisible(visible); right->setVisible(visible);
		down_left->setVisible(visible); down->setVisible(visible); down_right->setVisible(visible);
	}

	// Explicit draw for manual lifecycle (FormSpec pattern)
	inline void draw()
	{
		if (!isInitialized())
			return;
		up_left->draw();   up->draw();   up_right->draw();
		left->draw();      center->draw(); right->draw();
		down_left->draw(); down->draw(); down_right->draw();
	}

	// Cleanup: for managed=hide, for manual=remove+drop+null
	inline void remove()
	{
		if (!isInitialized())
			return;
		gui::IGUIImage *parts[] = {
			up_left, up, up_right,
			left, center, right,
			down_left, down, down_right
		};

		if (m_managed) {
			setVisible(false);
		} else {
			for (auto *p : parts) {
				if (p) {
					p->remove();
					p->drop();
				}
			}
			up_left = up = up_right = nullptr;
			left = center = right = nullptr;
			down_left = down = down_right = nullptr;
		}
	}

	// Check if initialized (any part is non-null)
	inline bool isInitialized() const
	{
		return up_left != nullptr;
	}
};

#endif // IS_VOPI_ENGINE
