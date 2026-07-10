// Copyright (C) 2002-2012 Nikolaus Gebhardt
// Modified by Mustapha T.
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#include "guiEditBoxWithScrollbar.h"

#include "IGUISkin.h"
#include "IGUIEnvironment.h"
#include "IGUIFont.h"
#include "rect.h"

#include "guiScrollBar.h"
#if IS_VOPI_ENGINE
#include "porting.h" // getTimeMs() for inertial scrolling
#include <algorithm>
#include <cmath>
#endif

using namespace gui;

//! constructor
GUIEditBoxWithScrollBar::GUIEditBoxWithScrollBar(const wchar_t* text, bool border,
	IGUIEnvironment* environment, IGUIElement* parent, s32 id,
	const core::rect<s32>& rectangle, ISimpleTextureSource *tsrc,
	bool writable, bool has_vscrollbar)
	: CGUIEditBox(text, border, environment, parent, id, rectangle),
	m_bg_color_used(false), m_tsrc(tsrc)
{
	if (has_vscrollbar) {
		createVScrollBar();

		calculateFrameRect();
		breakText();

		calculateScrollPos();
	}
	setWritable(writable);
}

//! draws the element and its children
void GUIEditBoxWithScrollBar::draw()
{
	if (!IsVisible)
		return;

	IGUISkin *skin = Environment->getSkin();
	if (!skin)
		return;

	if (m_bg_color_used) {
		OverrideBgColor = m_bg_color;
	} else if (IsWritable) {
		OverrideBgColor = skin->getColor(EGDC_WINDOW);
	} else {
		// Transparent
		OverrideBgColor = 0x00000001;
	}

	CGUIEditBox::draw();
}

//! create a vertical scroll bar
void GUIEditBoxWithScrollBar::createVScrollBar()
{
	IGUISkin *skin = 0;
	if (Environment)
		skin = Environment->getSkin();
	if (!skin || VScrollBar)
		return;

	s32 fontHeight = 1;

	if (OverrideFont) {
		fontHeight = OverrideFont->getDimension(L"Ay").Height;
	} else {
		IGUIFont *font;
		if ((font = skin->getFont())) {
			fontHeight = font->getDimension(L"Ay").Height;
		}
	}

	VScrollBarWidth = skin->getSize(gui::EGDS_SCROLLBAR_SIZE);

	core::rect<s32> scrollbarrect = RelativeRect;
	scrollbarrect.UpperLeftCorner.X += RelativeRect.getWidth() - VScrollBarWidth;
	VScrollBar = new GUIScrollBar(Environment, getParent(), -1,
			scrollbarrect, false, m_tsrc);

	VScrollBar->setVisible(false);
	VScrollBar->setSmallStep(3 * fontHeight);
	VScrollBar->setLargeStep(10 * fontHeight);
}

//! Change the background color
void GUIEditBoxWithScrollBar::setBackgroundColor(const video::SColor &bg_color)
{
	m_bg_color = bg_color;
	m_bg_color_used = true;
}

#if IS_VOPI_ENGINE
//! Sets the scrollbar style
void GUIEditBoxWithScrollBar::setScrollbarStyle(const StyleSpec &style, ISimpleTextureSource *tsrc)
{
	if (VScrollBar) {
		GUIScrollBar *scrollbar = static_cast<GUIScrollBar*>(VScrollBar);
		if (scrollbar) {
			scrollbar->setArrowsVisible(false);
			scrollbar->setStyle(style, tsrc);
		}
	}
}

// --- Touch drag-to-scroll (VOPI Engine, ITouchScrollTarget) ---

using namespace touch_scroll; // shared fling tuning (touchScrollTarget.h)

bool GUIEditBoxWithScrollBar::OnEvent(const SEvent &event)
{
	// Read-only multiline boxes: a pointer drag pans the text instead of
	// selecting it, and double/triple clicks don't select words/lines. On
	// touch screens the formspec gesture layer pans scrollable boxes before
	// events get here; this path covers desktop mice and non-scrollable boxes
	// (so a swipe over short text doesn't paint a useless selection either).
	// Editable boxes keep stock selection behavior.
	if (!IsWritable && isEnabled() && (WordWrap || MultiLine) &&
			event.EventType == EET_MOUSE_INPUT_EVENT) {
		const v2s32 p(event.MouseInput.X, event.MouseInput.Y);
		switch (event.MouseInput.Event) {
		case EMIE_LMOUSE_PRESSED_DOWN:
			stopFling();
			if (AbsoluteClippingRect.isPointInside(p)) {
				m_panning = true;
				m_pan_origin_y = p.Y;
				m_pan_origin_scrollpos = VScrollPos;
			}
			return true; // never start mouse marking
		case EMIE_MOUSE_MOVED:
			if (m_panning) {
				if (!event.MouseInput.isLeftPressed()) {
					// The release happened outside the element: end the pan.
					m_panning = false;
					break;
				}
				scrollByPixels(m_pan_origin_scrollpos,
						v2s32(0, p.Y - m_pan_origin_y));
				return true;
			}
			break;
		case EMIE_LMOUSE_LEFT_UP:
			m_panning = false;
			return true; // no cursor placement, no marking
		case EMIE_LMOUSE_DOUBLE_CLICK:
		case EMIE_LMOUSE_TRIPLE_CLICK:
			return true; // no word/line selection
		case EMIE_MOUSE_WHEEL:
			// The base class wheel-scrolls only through a visible scrollbar;
			// cover the scrollbar-less (style scrollbar_visible=false) case.
			if (!VScrollBar && isScrollable()) {
				s32 step = 10;
				if (IGUIFont *font = getActiveFont())
					step = 3 * (s32)font->getDimension(L"Ay").Height;
				setScrollPosClamped(VScrollPos -
						(s32)(event.MouseInput.Wheel * (f32)step));
				return true;
			}
			break;
		default:
			break;
		}
	}
	return CGUIEditBox::OnEvent(event);
}

void GUIEditBoxWithScrollBar::OnPostRender(u32 timeMs)
{
	if (m_flinging)
		stepFling();
	CGUIEditBox::OnPostRender(timeMs);
}

s32 GUIEditBoxWithScrollBar::scrollRangePixels()
{
	return std::max(0, (s32)getTextDimension().Height - FrameRect.getHeight());
}

bool GUIEditBoxWithScrollBar::isScrollable()
{
	return scrollRangePixels() > 0;
}

void GUIEditBoxWithScrollBar::setScrollPosClamped(s32 pos)
{
	VScrollPos = core::s32_clamp(pos, 0, scrollRangePixels());
	if (VScrollBar)
		VScrollBar->setPos(VScrollPos);
}

void GUIEditBoxWithScrollBar::scrollByPixels(s32 origin_scrollpos, const v2s32 &pixel_delta)
{
	// Content follows the finger: dragging down reveals earlier text, i.e.
	// decreases the scroll offset (the box scrolls vertically only).
	setScrollPosClamped(origin_scrollpos - pixel_delta.Y);
}

void GUIEditBoxWithScrollBar::startFling(f32 axis_velocity_px_per_ms)
{
	if (!isScrollable())
		return;
	if (std::fabs(axis_velocity_px_per_ms) < FLING_MIN_START_SPEED) {
		stopFling();
		return;
	}
	m_fling_vel = std::max(-FLING_MAX_SPEED,
			std::min(axis_velocity_px_per_ms, FLING_MAX_SPEED));
	// Start from the current offset so the hand-off from the drag is seamless.
	m_fling_px = (f32)VScrollPos;
	m_fling_last_ms = porting::getTimeMs();
	m_flinging = true;
}

void GUIEditBoxWithScrollBar::stopFling()
{
	m_flinging = false;
	m_fling_vel = 0.0f;
}

void GUIEditBoxWithScrollBar::stepFling()
{
	const u64 now = porting::getTimeMs();
	u64 dt = now - m_fling_last_ms;
	m_fling_last_ms = now;
	if (dt == 0)
		return;
	if (dt > FLING_MAX_STEP_MS)
		dt = FLING_MAX_STEP_MS; // clamp after a hitch so the text can't teleport

	// Finger velocity is positive downwards and a downward drag decreases the
	// scroll offset, so the glide continues in that same direction.
	m_fling_px -= m_fling_vel * (f32)dt;

	const f32 hi = (f32)scrollRangePixels();
	bool hit_bound = false;
	if (m_fling_px <= 0.0f) {
		m_fling_px = 0.0f;
		hit_bound = true;
	} else if (m_fling_px >= hi) {
		m_fling_px = hi;
		hit_bound = true;
	}

	setScrollPosClamped((s32)std::lround(m_fling_px));

	// Exponential friction, frame-rate independent.
	m_fling_vel *= std::pow(FLING_DECAY_PER_FRAME, (f32)dt / 16.667f);
	if (hit_bound || std::fabs(m_fling_vel) < FLING_MIN_SPEED)
		stopFling();
}

bool GUIEditBoxWithScrollBar::isPointOverScrollbar(const v2s32 &p) const
{
	return VScrollBar && VScrollBar->isVisible() &&
			VScrollBar->getAbsoluteClippingRect().isPointInside(p);
}
#endif
