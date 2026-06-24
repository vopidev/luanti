// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include "statusTextHelper.h"

#include <irrlicht_changes/static_text.h>

#include "client/renderingengine.h"
#if IS_VOPI_ENGINE
#include "client/fontengine.h"
#include "util/numeric.h" // rangelim
#include <algorithm>
#include <cmath>
#endif

StatusTextHelper::StatusTextHelper(gui::IGUIEnvironment *guienv, gui::IGUIElement *parent)
{
	if (!guienv)
		return;

	gui::IGUIElement *root = parent ? parent : guienv->getRootGUIElement();

	m_guitext_status = grab(gui::StaticText::add(guienv, L"",
			core::recti(), false, false, root));
	m_guitext_status->setVisible(false);

	// Initialize text color from skin
	if (guienv->getSkin())
		m_text_color = guienv->getSkin()->getColor(gui::EGDC_BUTTON_TEXT);
	else
		m_text_color = video::SColor(255, 0, 0, 0);
}

StatusTextHelper::~StatusTextHelper()
{
	if (m_guitext_status) {
		m_guitext_status->remove();
		m_guitext_status.reset();
	}
}

void StatusTextHelper::showStatusText(const std::wstring &str)
{
	m_statustext = str;
	m_statustext_time = 0.0f;
	m_fade_progress = 0.0f;
}

void StatusTextHelper::clearStatusText()
{
	m_statustext.clear();
	m_statustext_time = 0.0f;
	m_fade_progress = 0.0f;
	if (m_guitext_status)
		m_guitext_status->setVisible(false);
}

void StatusTextHelper::setVisible(bool visible)
{
	if (m_guitext_status)
		m_guitext_status->setVisible(visible);
}

bool StatusTextHelper::isVisible() const
{
	return m_guitext_status && m_guitext_status->isVisible();
}

void StatusTextHelper::setGameStyle()
{
	m_display_duration = 1.5f;
	m_background_enabled = false;
	m_use_main_menu_position = false;
#if IS_VOPI_ENGINE
	// VOPI: text is vertically centered inside the 9-slice background.
	m_text_alignment_v = gui::EGUIA_CENTER;
#else
	// in-game: top-anchored vertically
	m_text_alignment_v = gui::EGUIA_UPPERLEFT;
#endif
}

void StatusTextHelper::setMainMenuStyle()
{
	m_display_duration = 3.0f;
	m_background_color = video::SColor(220, 0, 0, 0);
	m_background_enabled = true;
	m_use_main_menu_position = true;
	// main menu: centered vertically
	m_text_alignment_v = gui::EGUIA_CENTER;
}

void StatusTextHelper::update(float dtime)
{
	if (!m_guitext_status || m_statustext.empty())
		return;

#if IS_VOPI_ENGINE
	// Apply the cached font override once the element exists. update() owns the
	// inner element's visual state, so the override is (re)applied here.
	if (m_override_font && !m_override_font_applied) {
		m_guitext_status->setOverrideFont(m_override_font);
		m_override_font_applied = true;
	}
#endif

	m_statustext_time += dtime;

	if (m_statustext_time >= m_display_duration) {
		clearStatusText();
		return;
	}

	m_fade_progress = m_statustext_time / m_display_duration;

	// update() is the sole owner of the GUI element's visual state.
	// showStatusText() only resets the timers; all setText / color /
	// position work happens here.
	m_guitext_status->setText(m_statustext.c_str());
	m_guitext_status->setVisible(true);
	m_guitext_status->setTextAlignment(gui::EGUIA_CENTER, m_text_alignment_v);

	updatePosition();

	// Quadratic fade feels a bit smoother than linear.
	const f32 alpha_factor = 1.0f - m_fade_progress * m_fade_progress;

	// Background (optional)
	if (m_background_enabled) {
		video::SColor bg_fade = m_background_color;
		bg_fade.setAlpha(static_cast<u32>(bg_fade.getAlpha() * alpha_factor));
		m_guitext_status->setBackgroundColor(bg_fade);
		m_guitext_status->setDrawBackground(true);
	} else {
		m_guitext_status->setDrawBackground(false);
	}

	// Text color fade
	video::SColor text_fade = m_text_color;
	text_fade.setAlpha(static_cast<u32>(text_fade.getAlpha() * alpha_factor));
	m_guitext_status->setOverrideColor(text_fade);
	m_guitext_status->enableOverrideColor(true);
}

void StatusTextHelper::updatePosition()
{
	if (!m_guitext_status)
		return;

	v2u32 screensize = RenderingEngine::getWindowSize();
	s32 text_width = m_guitext_status->getTextWidth();
	s32 text_height = m_guitext_status->getTextHeight();

	if (m_use_main_menu_position) {
		// Full-width bar at bottom (main menu style)
		const s32 bar_height = MAIN_MENU_BAR_HEIGHT;
		m_guitext_status->setRelativePosition(core::rect<s32>(
				0,
				(s32)screensize.Y - bar_height,
				(s32)screensize.X,
				(s32)screensize.Y));
	} else {
#if IS_VOPI_ENGINE
		// VOPI: text centered inside a 9-slice background, positioned a
		// configurable fraction of the screen height above the bottom edge.
#if defined(__ANDROID__) || defined(__IOS__)
		const s32 padding = 10; // Padding for mobile
#else
		const s32 padding = 5;  // Padding for desktop
#endif
		const s32 central_height = text_height + (2 * padding);
		// Side (corner) width, clamped to a sane range.
		s32 side_width = central_height / 4;
		side_width = std::max(5, std::min(side_width, 50));

		s32 total_width = text_width + (side_width * 2) + (2 * padding);
		total_width = std::min(total_width, (s32)screensize.X);
		const s32 total_height = central_height;

		const s32 status_y = (s32)screensize.Y -
				(s32)((f32)screensize.Y * m_status_text_bottom_offset);
		const s32 status_x = ((s32)screensize.X - total_width) / 2;

		m_guitext_status->setRelativePosition(core::rect<s32>(
				status_x + side_width,
				status_y + padding,
				status_x + total_width - side_width,
				status_y + total_height - padding));

		// Cache the background geometry for positionBackground() (same frame).
		m_bg_rect = core::rect<s32>(status_x, status_y,
				status_x + total_width, status_y + total_height);
		m_bg_corner_size = side_width;
#else
		// Centered above bottom (game style)
		const s32 status_y = (s32)screensize.Y - 150;
		const s32 status_x = ((s32)screensize.X - text_width) / 2;
		m_guitext_status->setRelativePosition(core::rect<s32>(
				status_x,
				status_y - text_height,
				status_x + text_width,
				status_y));
#endif
	}
}

#if IS_VOPI_ENGINE
void StatusTextHelper::setFontScale(f32 scale)
{
	if (!g_fontengine)
		return;

	const u16 base_font_size = g_fontengine->getDefaultFontSize();
	const u16 font_size = std::round(base_font_size * scale);
	if (font_size == 0)
		return;

	m_override_font = g_fontengine->getFont(
			rangelim(font_size, 5, 72), FM_Unspecified);
	// Force (re)application on the next update().
	m_override_font_applied = false;
}

void StatusTextHelper::positionBackground(NineSliceBackground &bg, bool show)
{
	// update() recomputes m_bg_rect each frame while the text is visible and is
	// the sole owner of the inner element's geometry; we only consume the rect
	// it produced this frame here, so the per-frame rewrite can't clobber us.
	const bool visible = show && isVisible();
	bg.setVisible(visible);
	if (visible)
		bg.setPosition(m_bg_rect, m_bg_corner_size);
}

void StatusTextHelper::bringToFront()
{
	if (m_guitext_status && m_guitext_status->getParent())
		m_guitext_status->getParent()->bringToFront(m_guitext_status.get());
}
#endif
