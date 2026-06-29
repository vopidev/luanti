// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2020 DS

#include "guiScrollContainer.h"
#include <IGUIEnvironment.h>

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
bool GUIScrollContainer::isScrollable() const
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

	// setPos() clamps to [min, max] internally (CGUIScrollBar::setPosRaw).
	const s32 old_pos = m_scrollbar->getPos();
	m_scrollbar->setPos(new_pos);
	if (m_scrollbar->getPos() != old_pos)
		updateScrolling();
}
#endif
