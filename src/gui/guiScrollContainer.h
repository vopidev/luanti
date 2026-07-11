// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2020 DS

#pragma once

#include "guiScrollBar.h"
#if IS_VOPI_ENGINE
#include "touchScrollTarget.h"
#endif

class GUIScrollContainer : public gui::IGUIElement
#if IS_VOPI_ENGINE
		, public ITouchScrollTarget
#endif
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
	// --- Touch drag-to-scroll support (VOPI Engine, ITouchScrollTarget) ---

	// True if the attached scrollbar currently has a non-empty scroll range.
	bool isScrollable() override;

	// Current scrollbar position (0 if no scrollbar is attached).
	inline s32 getScrollPos() const override
	{
		return m_scrollbar ? m_scrollbar->getPos() : 0;
	}

	// Projects a pointer delta onto the container's scroll axis.
	inline s32 axisDelta(const v2s32 &delta) const override
	{
		return m_orientation == HORIZONTAL ? delta.X : delta.Y;
	}

	// Projects a pointer delta onto the axis perpendicular to scrolling.
	inline s32 crossAxisDelta(const v2s32 &delta) const override
	{
		return m_orientation == HORIZONTAL ? delta.Y : delta.X;
	}

	// Pans the content by a pixel delta along the orientation, relative to the
	// given reference scrollbar position. The scrollbar clamps to its range.
	void scrollByPixels(s32 origin_scrollpos, const v2s32 &pixel_delta) override;

	// Starts inertial scrolling (a "fling") with the given finger velocity in
	// pixels per millisecond along the scroll axis. A small velocity is ignored.
	void startFling(f32 axis_velocity_px_per_ms) override;
	// Stops any in-flight inertial scrolling.
	void stopFling() override;
	inline bool isFlinging() const override { return m_fling.active; }

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
	// Inertial scrolling: the shared integrator (touchScrollTarget.h) glides in
	// floating-point pixel space so the content moves smoothly (1 px steps)
	// instead of jumping in coarse scrollbar-position units; the integer
	// scrollbar position (the thumb) is re-synced from the pixel offset each
	// frame.
	void stepFling();
	// Sets the content (mover) offset along the scroll axis directly, in pixels,
	// skipping the child reposition when it is unchanged. Used by the fling for
	// sub-quantum-smooth motion.
	void setContentOffset(s32 offset_px);
	touch_scroll::FlingState m_fling;
	// Field sends during a fling are throttled: setPosAndSend() emits a scrollbar
	// change that a named scroll_container turns into a TOSERVER_INVENTORY_FIELDS
	// (on_player_receive_fields), which at frame rate for the whole momentum
	// glide would spam server mods. Between throttled sends the thumb is moved
	// silently; a pending change is flushed when the fling stops.
	u64 m_fling_last_send_ms = 0;
	bool m_fling_send_pending = false;
#endif

};
