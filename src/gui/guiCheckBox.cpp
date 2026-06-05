// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h
//
// VOPI: Derived from Irrlicht's CGUICheckBox; adds custom per-state textures.

#include "guiCheckBox.h"

#if IS_VOPI_ENGINE

#include "client/guiscalingfilter.h"
#include "porting.h"
#include "IGUISkin.h"
#include "IGUIEnvironment.h"
#include "IVideoDriver.h"
#include "IGUIFont.h"

GUICheckBox::GUICheckBox(bool checked, gui::IGUIEnvironment *environment,
		gui::IGUIElement *parent, s32 id, core::rect<s32> rectangle) :
		gui::IGUICheckBox(environment, parent, id, rectangle), m_checked(checked)
{
	// this element can be tabbed into
	setTabStop(true);
	setTabOrder(-1);
}

//! called if an event happened.
bool GUICheckBox::OnEvent(const SEvent &event)
{
	if (isEnabled()) {
		switch (event.EventType) {
		case EET_KEY_INPUT_EVENT:
			if (event.KeyInput.PressedDown &&
					(event.KeyInput.Key == KEY_RETURN || event.KeyInput.Key == KEY_SPACE)) {
				m_pressed = true;
				return true;
			} else if (m_pressed && event.KeyInput.PressedDown && event.KeyInput.Key == KEY_ESCAPE) {
				m_pressed = false;
				return true;
			} else if (!event.KeyInput.PressedDown && m_pressed &&
					   (event.KeyInput.Key == KEY_RETURN || event.KeyInput.Key == KEY_SPACE)) {
				m_pressed = false;
				if (Parent) {
					SEvent newEvent;
					newEvent.EventType = EET_GUI_EVENT;
					newEvent.GUIEvent.Caller = this;
					newEvent.GUIEvent.Element = 0;
					m_checked = !m_checked;
					newEvent.GUIEvent.EventType = gui::EGET_CHECKBOX_CHANGED;
					Parent->OnEvent(newEvent);
				}
				return true;
			}
			break;
		case EET_GUI_EVENT:
			if (event.GUIEvent.EventType == gui::EGET_ELEMENT_FOCUS_LOST) {
				if (event.GUIEvent.Caller == this)
					m_pressed = false;
			}
			break;
		case EET_MOUSE_INPUT_EVENT:
			if (event.MouseInput.Event == EMIE_LMOUSE_PRESSED_DOWN) {
				m_pressed = true;
				m_check_time = (u32)porting::getTimeMs();
				return true;
			} else if (event.MouseInput.Event == EMIE_LMOUSE_LEFT_UP) {
				bool wasPressed = m_pressed;
				m_pressed = false;

				if (wasPressed && Parent) {
					if (!AbsoluteClippingRect.isPointInside(core::position2d<s32>(event.MouseInput.X, event.MouseInput.Y))) {
						m_pressed = false;
						return true;
					}

					SEvent newEvent;
					newEvent.EventType = EET_GUI_EVENT;
					newEvent.GUIEvent.Caller = this;
					newEvent.GUIEvent.Element = 0;
					m_checked = !m_checked;
					newEvent.GUIEvent.EventType = gui::EGET_CHECKBOX_CHANGED;
					Parent->OnEvent(newEvent);
				}

				return true;
			}
			break;
		default:
			break;
		}
	}

	return IGUIElement::OnEvent(event);
}

//! draws the element and its children
void GUICheckBox::draw()
{
	if (!IsVisible)
		return;

	gui::IGUISkin *skin = Environment->getSkin();
	if (skin) {
		video::IVideoDriver *driver = Environment->getVideoDriver();
		core::rect<s32> frameRect(AbsoluteRect);

		// draw background
		if (m_background) {
			video::SColor bgColor = skin->getColor(gui::EGDC_3D_FACE);
			driver->draw2DRectangle(bgColor, frameRect, &AbsoluteClippingRect);
		}

		// draw the border
		if (m_border) {
			skin->draw3DSunkenPane(this, 0, true, false, frameRect, &AbsoluteClippingRect);
			frameRect.UpperLeftCorner.X += skin->getSize(gui::EGDS_TEXT_DISTANCE_X);
			frameRect.LowerRightCorner.X -= skin->getSize(gui::EGDS_TEXT_DISTANCE_X);
		}

		// VOPI: custom box size when set, else the skin checkbox width.
		const s32 height = m_box_size > 0
				? m_box_size
				: skin->getSize(gui::EGDS_CHECK_BOX_WIDTH);

		// the rectangle around the "checked" area.
		core::rect<s32> checkRect(frameRect.UpperLeftCorner.X,
				((frameRect.getHeight() - height) / 2) + frameRect.UpperLeftCorner.Y,
				0, 0);

		checkRect.LowerRightCorner.X = checkRect.UpperLeftCorner.X + height;
		checkRect.LowerRightCorner.Y = checkRect.UpperLeftCorner.Y + height;

		if (m_texture_unchecked && m_texture_checked) {
			// VOPI: draw the per-state custom texture in place of the skin box.
			video::ITexture *tex = m_checked ? m_texture_checked : m_texture_unchecked;
			const core::rect<s32> srcRect(core::position2d<s32>(0, 0),
					tex->getOriginalSize());
			const video::SColor color(255, 255, 255, 255);
			const video::SColor colors[] = {color, color, color, color};
			draw2DImageFilterScaled(driver, tex, checkRect, srcRect,
					NoClip ? nullptr : &AbsoluteClippingRect, colors, true);
		} else {
			gui::EGUI_DEFAULT_COLOR col = gui::EGDC_GRAY_EDITABLE;
			if (isEnabled())
				col = m_pressed ? gui::EGDC_FOCUSED_EDITABLE : gui::EGDC_EDITABLE;
			skin->draw3DSunkenPane(this, skin->getColor(col),
					false, true, checkRect, &AbsoluteClippingRect);

			// the checked icon
			if (m_checked) {
				skin->drawIcon(this, gui::EGDI_CHECK_BOX_CHECKED, checkRect.getCenter(),
						m_check_time, (u32)porting::getTimeMs(), false, &AbsoluteClippingRect);
			}
		}

		// associated text
		if (Text.size()) {
			core::rect<s32> textRect = frameRect;
			textRect.UpperLeftCorner.X += height + 5;
			// VOPI: optional vertical nudge so the label lines up with the box
			// (font line-box centring otherwise reads slightly high).
			textRect.UpperLeftCorner.Y += m_text_voffset;
			textRect.LowerRightCorner.Y += m_text_voffset;

			// VOPI: custom label font when set, else the skin font.
			gui::IGUIFont *font = m_override_font ? m_override_font : skin->getFont();
			if (font) {
				// VOPI: custom label colour when set (and enabled), else the
				// skin colour (button-text / gray-when-disabled).
				const video::SColor text_color =
						(m_has_override_color && isEnabled())
						? m_override_color
						: skin->getColor(isEnabled() ? gui::EGDC_BUTTON_TEXT
													 : gui::EGDC_GRAY_TEXT);
				font->draw(Text.c_str(), textRect, text_color,
						false, true, &AbsoluteClippingRect);
			}
		}
	}
	IGUIElement::draw();
}

//! set if box is checked
void GUICheckBox::setChecked(bool checked)
{
	m_checked = checked;
}

//! returns if box is checked
bool GUICheckBox::isChecked() const
{
	return m_checked;
}

//! Sets whether to draw the background
void GUICheckBox::setDrawBackground(bool draw)
{
	m_background = draw;
}

//! Checks if background drawing is enabled
bool GUICheckBox::isDrawBackgroundEnabled() const
{
	return m_background;
}

//! Sets whether to draw the border
void GUICheckBox::setDrawBorder(bool draw)
{
	m_border = draw;
}

//! Checks if border drawing is enabled
bool GUICheckBox::isDrawBorderEnabled() const
{
	return m_border;
}

#endif // IS_VOPI_ENGINE
