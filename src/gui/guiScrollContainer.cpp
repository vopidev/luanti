// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2020 DS

#include "guiScrollContainer.h"
#include <IGUIEnvironment.h>
#if IS_VOPI_ENGINE
#include "porting.h" // getTimeMs() for inertial scrolling
#include <cmath>
#endif

GUIScrollContainer::GUIScrollContainer(gui::IGUIEnvironment *env,
		gui::IGUIElement *parent, s32 id, const core::rect<s32> &rectangle,
		const std::string &orientation, f32 scrollfactor) :
		gui::IGUIElement(gui::EGUIET_ELEMENT, env, parent, id, rectangle),
		m_scrollbar(nullptr), m_scrollfactor(scrollfactor)
{
	if (orientation == "vertical")
		m_orientation = VERTICAL;
	else if (orientation == "horizontal")
		m_orientation = HORIZONTAL;
	else
		m_orientation = UNDEFINED;
}

bool GUIScrollContainer::OnEvent(const SEvent &event)
{
	if (event.EventType == EET_MOUSE_INPUT_EVENT &&
			event.MouseInput.Event == EMIE_MOUSE_WHEEL &&
			!event.MouseInput.isLeftPressed() && m_scrollbar) {
		Environment->setFocus(m_scrollbar);
		bool retval = m_scrollbar->OnEvent(event);

		// a hacky fix for updating the hovering and co.
		IGUIElement *hovered_elem = getElementFromPoint(core::position2d<s32>(
				event.MouseInput.X, event.MouseInput.Y));
		SEvent mov_event = event;
		mov_event.MouseInput.Event = EMIE_MOUSE_MOVED;
		Environment->postEventFromUser(mov_event);
		if (hovered_elem)
			hovered_elem->OnEvent(mov_event);

		return retval;
	}

	return IGUIElement::OnEvent(event);
}

void GUIScrollContainer::draw()
{
	if (isVisible()) {
		for (auto child : Children)
			if (child->isNotClipped() ||
					AbsoluteClippingRect.isRectCollided(
							child->getAbsolutePosition()))
				child->draw();
	}
}

void GUIScrollContainer::setScrollBar(GUIScrollBar *scrollbar)
{
	m_scrollbar = scrollbar;

	if (m_scrollbar && m_content_padding_px.has_value() && m_scrollfactor != 0.0f) {
		// Set the scrollbar max value based on the content size.

		// Get content size based on elements
		core::rect<s32> size;
		for (gui::IGUIElement *e : Children) {
			core::rect<s32> abs_rect = e->getAbsolutePosition();
			size.addInternalPoint(abs_rect.LowerRightCorner);
		}

		s32 visible_content_px = (
			m_orientation == VERTICAL
				? AbsoluteClippingRect.getHeight()
				: AbsoluteClippingRect.getWidth()
		);

		s32 total_content_px = *m_content_padding_px + (
			m_orientation == VERTICAL
				? (size.LowerRightCorner.Y - AbsoluteClippingRect.UpperLeftCorner.Y)
				: (size.LowerRightCorner.X - AbsoluteClippingRect.UpperLeftCorner.X)
		);

		s32 hidden_content_px = std::max<s32>(0, total_content_px - visible_content_px);
		m_scrollbar->setMin(0);
		m_scrollbar->setMax(std::ceil(hidden_content_px / std::fabs(m_scrollfactor)));

		// Note: generally, the scrollbar has the same size as the scroll container.
		// However, in case it isn't, proportional adjustments are needed.
		s32 scrollbar_px = (
			m_scrollbar->isHorizontal()
				? m_scrollbar->getRelativePosition().getWidth()
				: m_scrollbar->getRelativePosition().getHeight()
		);

		m_scrollbar->setPageSize((total_content_px * scrollbar_px) / visible_content_px);
	}
}

void GUIScrollContainer::updateScrolling()
{
	s32 pos = m_scrollbar->getPos();
	core::rect<s32> rect = getRelativePosition();

	if (m_orientation == VERTICAL)
		rect.UpperLeftCorner.Y = pos * m_scrollfactor;
	else if (m_orientation == HORIZONTAL)
		rect.UpperLeftCorner.X = pos * m_scrollfactor;

	setRelativePosition(rect);
}

#if IS_VOPI_ENGINE
bool GUIScrollContainer::isScrollable()
{
	return m_scrollbar && m_scrollfactor != 0.0f &&
			m_scrollbar->getMax() > m_scrollbar->getMin();
}

void GUIScrollContainer::scrollByPixels(s32 origin_scrollpos, const v2s32 &pixel_delta)
{
	if (!m_scrollbar || m_scrollfactor == 0.0f)
		return;

	// Content follows the finger: shifting the mover by `axis_px` pixels means
	// changing the scrollbar position by axis_px / scrollfactor. This mirrors
	// the scroll-into-view math in guiFormSpecMenu. m_scrollfactor is negative
	// for the supported (positive) scroll_factor, so dragging in the positive
	// direction decreases the position, as expected.
	s32 axis_px = axisDelta(pixel_delta);
	s32 new_pos = origin_scrollpos + (s32)std::round(axis_px / m_scrollfactor);

	// Drive the scrollbar via setPosAndSend (it clamps internally and emits
	// EGET_SCROLL_BAR_CHANGED only when the position changes) so a touch drag
	// behaves exactly like moving the scrollbar: the formspec handler
	// repositions the bound container(s) and notifies the server, so the scroll
	// position survives a server-driven formspec rebuild.
	if (new_pos != m_scrollbar->getPos())
		m_scrollbar->setPosAndSend(new_pos);
}

namespace {
	// Minimum gap between field-send scrollbar updates during a fling (ms).
	constexpr u64 FLING_SEND_INTERVAL_MS = 100;
}

void GUIScrollContainer::startFling(f32 axis_velocity_px_per_ms)
{
	if (!isScrollable())
		return;
	// Seed from the current (quantised) content offset so the hand-off from
	// the drag is seamless. The finger velocity applies to the content offset
	// directly (offset = pos * scrollfactor), so no sign flip is needed.
	if (!m_fling.start(axis_velocity_px_per_ms,
			(f32)m_scrollbar->getPos() * m_scrollfactor, porting::getTimeMs()))
		stopFling();
}

void GUIScrollContainer::stopFling()
{
	m_fling.stop();
	// Flush a throttled-away position change so the server ends up with the
	// final scroll position even if the last step only moved the thumb silently
	// (setPosAndSend would be a no-op here since the position already matches).
	if (m_fling_send_pending && m_scrollbar) {
		m_scrollbar->sendChanged();
		m_fling_send_pending = false;
	}
}

void GUIScrollContainer::setContentOffset(s32 offset_px)
{
	core::rect<s32> rect = getRelativePosition();
	if (m_orientation == VERTICAL) {
		if (rect.UpperLeftCorner.Y == offset_px)
			return;
		rect.UpperLeftCorner.Y = offset_px;
	} else if (m_orientation == HORIZONTAL) {
		if (rect.UpperLeftCorner.X == offset_px)
			return;
		rect.UpperLeftCorner.X = offset_px;
	} else {
		return; // no defined scroll axis
	}
	setRelativePosition(rect);
}

void GUIScrollContainer::stepFling()
{
	if (!m_scrollbar) {
		stopFling();
		return;
	}

	// Advance the shared integrator, clamped to the content-offset range
	// (endpoints are min/max * scrollfactor).
	const u64 now = porting::getTimeMs();
	const f32 end_a = (f32)m_scrollbar->getMax() * m_scrollfactor;
	const f32 end_b = (f32)m_scrollbar->getMin() * m_scrollfactor;
	const bool gliding = m_fling.step(now,
			std::min(end_a, end_b), std::max(end_a, end_b));

	// Sync the integer scrollbar position and notify like a real scrollbar move
	// (setPosAndSend emits EGET_SCROLL_BAR_CHANGED when the position changes), so
	// momentum reaches the server / bound containers the same way a scrollbar
	// drag does and the position survives a formspec rebuild.
	const s32 pos = (s32)std::lround(m_fling.px / m_scrollfactor);
	if (pos != m_scrollbar->getPos()) {
		// Throttle the field-send: send at most every FLING_SEND_INTERVAL_MS,
		// and move the thumb silently in between so the glide stays smooth
		// without a per-frame TOSERVER_INVENTORY_FIELDS. A stopping fling
		// (bound hit / too slow) always sends. stopFling() flushes any
		// pending change.
		if (!gliding || now - m_fling_last_send_ms >= FLING_SEND_INTERVAL_MS) {
			m_scrollbar->setPosAndSend(pos);
			m_fling_last_send_ms = now;
			m_fling_send_pending = false;
		} else {
			m_scrollbar->setPos(pos);
			m_fling_send_pending = true;
		}
	}

	// Re-apply the smooth sub-quantum pixel offset: the EGET handler above
	// repositions the content to the coarse scrollbar quantum, so override it to
	// keep the glide 1 px smooth (the helper skips an unchanged recompute).
	setContentOffset((s32)std::lround(m_fling.px));

	if (!gliding)
		stopFling();
}

void GUIScrollContainer::OnPostRender(u32 timeMs)
{
	if (isFlinging())
		stepFling();
	IGUIElement::OnPostRender(timeMs);
}
#endif
