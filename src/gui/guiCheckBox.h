// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h
//
// VOPI: Derived from Irrlicht's CGUICheckBox to add optional custom textures
// for the unchecked / checked states. Lives engine-side (src/gui) so the
// vendored irr/ Irrlicht layer stays untouched.

#pragma once

#if IS_VOPI_ENGINE

#include "IGUICheckBox.h"
#include "IGUIFont.h"
#include "ITexture.h"
#include "SColor.h"

// VOPI: A checkbox that can render two custom textures (one per state) instead
// of the default skin pane + check icon. When both textures are unset it falls
// back to the stock Irrlicht rendering, so it is a drop-in replacement for the
// engine's CGUICheckBox and stays compatible with the formspec read-back path
// (it reports the gui::EGUIET_CHECK_BOX type via IGUICheckBox).
class GUICheckBox : public gui::IGUICheckBox
{
public:
	GUICheckBox(bool checked, gui::IGUIEnvironment *environment,
			gui::IGUIElement *parent, s32 id, core::rect<s32> rectangle);

	//! set if box is checked
	void setChecked(bool checked) override;

	//! returns if box is checked
	bool isChecked() const override;

	//! Sets whether to draw the background
	void setDrawBackground(bool draw) override;

	//! Checks if background drawing is enabled
	bool isDrawBackgroundEnabled() const override;

	//! Sets whether to draw the border
	void setDrawBorder(bool draw) override;

	//! Checks if border drawing is enabled
	bool isDrawBorderEnabled() const override;

	//! called if an event happened.
	bool OnEvent(const SEvent &event) override;

	//! draws the element and its children
	void draw() override;

	// VOPI: Textures for the unchecked / checked states. Both must be non-null
	// to enable image rendering; otherwise the default skin box is drawn.
	void setImages(video::ITexture *unchecked, video::ITexture *checked)
	{
		m_texture_unchecked = unchecked;
		m_texture_checked = checked;
	}

	// VOPI: Override the box edge length (pixels). 0 keeps the skin checkbox
	// width (EGDS_CHECK_BOX_WIDTH).
	void setBoxSize(s32 size_px) { m_box_size = size_px; }

	// VOPI: Override the label colour. When unset, the skin button-text colour
	// is used (stock behaviour).
	void setOverrideColor(video::SColor color)
	{
		m_override_color = color;
		m_has_override_color = true;
	}

	// VOPI: Override the label font (e.g. a font_size-scaled font). When unset
	// (nullptr) the skin font is used.
	void setOverrideFont(gui::IGUIFont *font) { m_override_font = font; }

	// VOPI: Nudge the label vertically (pixels; + = down, - = up). Compensates
	// for font metrics (the line box includes descent, so vertically-centred
	// text reads slightly high vs the box). 0 = no nudge.
	void setTextVOffset(s32 px) { m_text_voffset = px; }

private:
	u32 m_check_time = 0;
	bool m_pressed = false;
	bool m_checked;
	bool m_border = false;
	bool m_background = false;

	video::ITexture *m_texture_unchecked = nullptr;
	video::ITexture *m_texture_checked = nullptr;

	s32 m_box_size = 0;                  // 0 = use the skin checkbox width
	video::SColor m_override_color = video::SColor(255, 255, 255, 255);
	bool m_has_override_color = false;
	gui::IGUIFont *m_override_font = nullptr;  // nullptr = use the skin font
	s32 m_text_voffset = 0;              // label vertical nudge (px; + = down)
};

#endif // IS_VOPI_ENGINE
