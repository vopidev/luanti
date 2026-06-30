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

	// Starts inertial scrolling (a "fling") with the given finger velocity in
	// pixels per millisecond along the scroll axis. A small velocity is ignored.
	void startFling(f32 axis_velocity_px_per_ms);
	// Stops any in-flight inertial scrolling.
	void stopFling();
	inline bool isFlinging() const { return m_flinging; }

	// Per-frame hook: advances an in-flight fling. Called by the GUI environment.
	virtual void OnPostRender(u32 timeMs) override;
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

#if IS_VOPI_ENGINE
	// Inertial scrolling (fling) state. The fling is integrated in floating-point
	// pixel space so the content glides smoothly (1 px steps) instead of jumping
	// in coarse scrollbar-position units; the integer scrollbar position (the
	// thumb) is re-synced from the pixel offset each frame.
	void stepFling();
	// Sets the content (mover) offset along the scroll axis directly, in pixels,
	// skipping the child reposition when it is unchanged. Used by the fling for
	// sub-quantum-smooth motion.
	void setContentOffset(s32 offset_px);
	bool m_flinging = false;
	f32 m_fling_vel = 0.0f; //< axis velocity, pixels per millisecond
	f32 m_fling_px = 0.0f;  //< current content offset along the axis, in pixels
	u64 m_fling_last_ms = 0;
#endif

};
