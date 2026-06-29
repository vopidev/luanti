// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2020 DS

#pragma once

#include "guiScrollBar.h"

class GUIScrollContainer : public gui::IGUIElement
{
public:
	GUIScrollContainer(gui::IGUIEnvironment *env, gui::IGUIElement *parent, s32 id,
			const core::rect<s32> &rectangle, const std::string &orientation,
			f32 scrollfactor);

	virtual bool OnEvent(const SEvent &event) override;

	virtual void draw() override;

	inline void setContentPadding(std::optional<s32> padding)
	{
		m_content_padding_px = padding;
	}

	inline void onScrollEvent(gui::IGUIElement *caller)
	{
		if (caller == m_scrollbar)
			updateScrolling();
	}

	void setScrollBar(GUIScrollBar *scrollbar);
	void updateScrolling();

	inline f32 getScrollFactor() const
	{
		return m_scrollfactor;
	}

#if IS_VOPI_ENGINE
	// --- Touch drag-to-scroll support (VOPI Engine) ---

	// True if the attached scrollbar currently has a non-empty scroll range.
	bool isScrollable() const;

	// Current scrollbar position (0 if no scrollbar is attached).
	inline s32 getScrollPos() const
	{
		return m_scrollbar ? m_scrollbar->getPos() : 0;
	}

	// Projects a pointer delta onto the container's scroll axis.
	inline s32 axisDelta(const v2s32 &delta) const
	{
		return m_orientation == HORIZONTAL ? delta.X : delta.Y;
	}

	// Pans the content by a pixel delta along the orientation, relative to the
	// given reference scrollbar position. The scrollbar clamps to its range.
	void scrollByPixels(s32 origin_scrollpos, const v2s32 &pixel_delta);
#endif

private:
	enum OrientationEnum
	{
		VERTICAL,
		HORIZONTAL,
		UNDEFINED
	};

	GUIScrollBar *m_scrollbar;
	OrientationEnum m_orientation;
	f32 m_scrollfactor; //< scrollbar pos * scrollfactor = scroll offset in pixels
	std::optional<s32> m_content_padding_px; //< in pixels

};
