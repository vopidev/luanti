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

#if IS_VOPI_ENGINE
	// The base draw() re-breaks the text when the active font changed since
	// the last break (e.g. a skin font swap); drop the cached height with it.
	if (LastBreakFont != getActiveFont())
		invalidateTextHeight();
#endif

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
			if (!AbsoluteClippingRect.isPointInside(p)) {
				// A press whose coordinates lie outside the box only arrives
				// through focused-element dispatch. Mirror the base class:
				// when focused, fall through to it (it declines the press so
				// the parent formspec still sees e.g. click-outside-to-drop);
				// when not, swallow it like the base would — but without its
				// selection-marking side effect.
				if (Environment->hasFocus(this))
					break;
				return true;
			}
			stopFling();
			m_panning = true;
			m_pan_origin_y = p.Y;
			m_pan_origin_scrollpos = VScrollPos;
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
			if (m_panning) {
				m_panning = false;
				return true;
			}
			if (Environment->hasFocus(this))
				return true; // no cursor placement, no marking
			break; // not ours: bubbles via the base, like stock behavior
		case EMIE_LMOUSE_DOUBLE_CLICK:
		case EMIE_LMOUSE_TRIPLE_CLICK:
			return true; // no word/line selection
		case EMIE_MOUSE_WHEEL:
			// The base class wheel-scrolls only through a visible scrollbar;
			// cover the scrollbar-less (style scrollbar_visible=false) case.
			if (!VScrollBar && isScrollable()) {
				setScrollPosClamped(VScrollPos -
						(s32)(event.MouseInput.Wheel * (f32)wheelStepPixels()));
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
	if (isFlinging())
		stepFling();
	CGUIEditBox::OnPostRender(timeMs);
}

s32 GUIEditBoxWithScrollBar::textHeightPixels()
{
	// Measuring walks every wrapped line through the font engine, so pan/fling
	// hot paths must not pay it per event: read-only boxes cache the height
	// (their text only changes via the overridden mutators, which invalidate),
	// writable boxes measure fresh (their edits re-break the text through
	// non-virtual base paths).
	if (IsWritable)
		return (s32)getTextDimension().Height;
	if (m_text_height_cache < 0)
		m_text_height_cache = (s32)getTextDimension().Height;
	return m_text_height_cache;
}

s32 GUIEditBoxWithScrollBar::scrollRangePixels()
{
	return std::max(0, textHeightPixels() - FrameRect.getHeight());
}

bool GUIEditBoxWithScrollBar::isScrollable()
{
	return scrollRangePixels() > 0;
}

void GUIEditBoxWithScrollBar::setScrollPosClamped(s32 pos)
{
	const s32 clamped = core::s32_clamp(pos, 0, scrollRangePixels());
	if (clamped == VScrollPos && (!VScrollBar || VScrollBar->getPos() == clamped))
		return;
	VScrollPos = clamped;
	if (VScrollBar)
		VScrollBar->setPos(clamped);
}

s32 GUIEditBoxWithScrollBar::wheelStepPixels() const
{
	// Keep in sync with createVScrollBar(): one notch = the scrollbar's small
	// step (3 text lines), so wheel speed matches the scrollbar-driven path.
	s32 font_height = 1;
	if (IGUIFont *font = getActiveFont())
		font_height = (s32)font->getDimension(L"Ay").Height;
	return 3 * font_height;
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
	// Finger velocity is positive downwards while a downward drag decreases
	// VScrollPos, so the glide runs along the negated axis. Seeding from the
	// current offset keeps the drag-to-glide hand-off seamless.
	m_fling.start(-axis_velocity_px_per_ms, (f32)VScrollPos,
			porting::getTimeMs());
}

void GUIEditBoxWithScrollBar::stopFling()
{
	m_fling.stop();
}

void GUIEditBoxWithScrollBar::stepFling()
{
	m_fling.step(porting::getTimeMs(), 0.0f, (f32)scrollRangePixels());
	// Apply the (possibly final, clamped) position either way.
	setScrollPosClamped((s32)std::lround(m_fling.px));
}

bool GUIEditBoxWithScrollBar::isPointOverScrollbar(const v2s32 &p) const
{
	return VScrollBar && VScrollBar->isVisible() &&
			VScrollBar->getAbsoluteClippingRect().isPointInside(p);
}

// Mutators that re-break the text: keep the cached wrapped-text height honest.

void GUIEditBoxWithScrollBar::setText(const wchar_t *text)
{
	CGUIEditBox::setText(text);
	invalidateTextHeight();
}

void GUIEditBoxWithScrollBar::setOverrideFont(gui::IGUIFont *font)
{
	CGUIEditBox::setOverrideFont(font);
	invalidateTextHeight();
}

void GUIEditBoxWithScrollBar::setWordWrap(bool enable)
{
	CGUIEditBox::setWordWrap(enable);
	invalidateTextHeight();
}

void GUIEditBoxWithScrollBar::setMultiLine(bool enable)
{
	CGUIEditBox::setMultiLine(enable);
	invalidateTextHeight();
}

void GUIEditBoxWithScrollBar::updateAbsolutePosition()
{
	const core::rect<s32> old_rect = AbsoluteRect;
	CGUIEditBox::updateAbsolutePosition();
	// The base re-breaks the text only when the rect actually changed.
	if (old_rect != AbsoluteRect)
		invalidateTextHeight();
}
#endif
