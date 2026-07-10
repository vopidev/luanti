// Copyright (C) 2002-2012 Nikolaus Gebhardt, Modified by Mustapha Tachouct
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#ifndef GUIEDITBOXWITHSCROLLBAR_HEADER
#define GUIEDITBOXWITHSCROLLBAR_HEADER

#include "CGUIEditBox.h"

#if IS_VOPI_ENGINE
#include "StyleSpec.h"
#include "touchScrollTarget.h"
#endif

class ISimpleTextureSource;

class GUIEditBoxWithScrollBar : public gui::CGUIEditBox
#if IS_VOPI_ENGINE
	, public ITouchScrollTarget
#endif
{
public:

	//! constructor
	GUIEditBoxWithScrollBar(const wchar_t* text, bool border, gui::IGUIEnvironment* environment,
		IGUIElement* parent, s32 id, const core::rect<s32>& rectangle,
		ISimpleTextureSource *tsrc, bool writable = true, bool has_vscrollbar = true);

	//! destructor
	virtual ~GUIEditBoxWithScrollBar() {}

	//! draws the element and its children
	void draw() override;

	//! Change the background color
	void setBackgroundColor(const video::SColor &bg_color);

#if IS_VOPI_ENGINE
	//! Sets the scrollbar style
	void setScrollbarStyle(const StyleSpec &style, ISimpleTextureSource *tsrc);

	// --- Touch drag-to-scroll (VOPI Engine, ITouchScrollTarget) ---
	// A read-only multiline box pans its text on pointer drag instead of
	// selecting it (native mobile feel; the formspec touch gesture layer also
	// drives it through the interface below), glides on release (fling), and
	// keeps mouse-wheel scrolling when built without a scrollbar.

	bool OnEvent(const SEvent &event) override;
	// Per-frame hook: advances an in-flight fling. Called by the GUI environment.
	void OnPostRender(u32 timeMs) override;

	bool isScrollable() override;
	s32 getScrollPos() const override { return VScrollPos; }
	// The box scrolls vertically only.
	s32 axisDelta(const v2s32 &delta) const override { return delta.Y; }
	s32 crossAxisDelta(const v2s32 &delta) const override { return delta.X; }
	void scrollByPixels(s32 origin_scrollpos, const v2s32 &pixel_delta) override;
	void startFling(f32 axis_velocity_px_per_ms) override;
	void stopFling() override;
	bool isFlinging() const override { return m_flinging; }

	// True when p lies on the visible built-in scrollbar: the formspec touch
	// layer then leaves the press to the scrollbar's own thumb/track handling.
	bool isPointOverScrollbar(const v2s32 &p) const;
#endif

protected:
	//! create a Vertical ScrollBar
	void createVScrollBar();

#if IS_VOPI_ENGINE
	// Scrollable overflow in pixels; 0 when the text fits the frame. Matches
	// the updateVScrollBar() formula so pan/fling clamping always agrees with
	// the scrollbar's own max.
	s32 scrollRangePixels();
	// Clamps and applies a scroll position, keeping the scrollbar (when
	// present) in sync so updateVScrollBar() doesn't treat the difference as
	// a user scrollbar move and revert it on the next draw.
	void setScrollPosClamped(s32 pos);
	void stepFling();

	// Pointer pan state (read-only boxes: drag pans the text, no selection).
	bool m_panning = false;
	s32 m_pan_origin_y = 0;         // pointer y when the pan started
	s32 m_pan_origin_scrollpos = 0; // VScrollPos when the pan started

	// Inertial scrolling (fling), integrated in float pixel space for a
	// smooth glide (cf. GUIScrollContainer) and re-synced to VScrollPos
	// every frame.
	bool m_flinging = false;
	f32 m_fling_vel = 0.0f; //< axis velocity, pixels per millisecond
	f32 m_fling_px = 0.0f;  //< current scroll offset, in pixels
	u64 m_fling_last_ms = 0;
#endif

	bool m_bg_color_used;
	video::SColor m_bg_color;

	ISimpleTextureSource *m_tsrc;
};


#endif // GUIEDITBOXWITHSCROLLBAR_HEADER
