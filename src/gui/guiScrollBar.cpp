/*
Copyright (C) 2002-2013 Nikolaus Gebhardt
This file is part of the "Irrlicht Engine".
For conditions of distribution and use, see copyright notice in irrlicht.h
*/

#include "guiScrollBar.h"
#include "guiButton.h"
#if IS_VOPI_ENGINE
#include "client/guiscalingfilter.h"
#include "util/numeric.h"
#include <IVideoDriver.h>
#include <IGUIEnvironment.h>
#include <IGUISkin.h>
#include <algorithm>
#endif

GUIScrollBar::GUIScrollBar(IGUIEnvironment *environment, IGUIElement *parent, s32 id,
		core::rect<s32> rectangle, bool horizontal, ISimpleTextureSource *tsrc) :
		CGUIScrollBar(environment, parent, id, rectangle, horizontal)
{
	// We use GUIButton instead of CGUIButton
	if (UpButton) {
		UpButton->remove();
		UpButton->drop();
	}
	UpButton = new GUIButton(Environment, this, -1, {}, tsrc, NoClip);
	UpButton->setSubElement(true);
	UpButton->setTabStop(false);
	if (DownButton) {
		DownButton->remove();
		DownButton->drop();
	}
	DownButton = new GUIButton(Environment, this, -1, {}, tsrc, NoClip);
	DownButton->setSubElement(true);
	DownButton->setTabStop(false);

	refreshControls();
}

#if IS_VOPI_ENGINE
void GUIScrollBar::setTextures(const std::vector<video::ITexture *> &textures)
{
	m_textures = textures;
	refreshControls();
}

void GUIScrollBar::setArrowsVisible(bool visible)
{
	CGUIScrollBar::setArrowsVisible(visible ? SHOW : HIDE);
}

void GUIScrollBar::setStyle(const StyleSpec &style, ISimpleTextureSource *tsrc)
{
	if (style.isNotDefault(StyleSpec::SCROLLBAR_BGIMG) &&
			style.isNotDefault(StyleSpec::SCROLLBAR_THUMB_IMG) &&
			style.isNotDefault(StyleSpec::SCROLLBAR_TOP_IMG) &&
			style.isNotDefault(StyleSpec::SCROLLBAR_BOTTOM_IMG)) {
		std::vector<video::ITexture *> textures = {
			style.getTexture(StyleSpec::SCROLLBAR_BGIMG, tsrc),
			style.getTexture(StyleSpec::SCROLLBAR_THUMB_IMG, tsrc),
			style.getTexture(StyleSpec::SCROLLBAR_TOP_IMG, tsrc),
			style.getTexture(StyleSpec::SCROLLBAR_BOTTOM_IMG, tsrc)
		};
		if (style.isNotDefault(StyleSpec::SCROLLBAR_THUMB_TOP_IMG) &&
				style.isNotDefault(StyleSpec::SCROLLBAR_THUMB_BOTTOM_IMG)) {
			textures.push_back(style.getTexture(StyleSpec::SCROLLBAR_THUMB_TOP_IMG, tsrc));
			textures.push_back(style.getTexture(StyleSpec::SCROLLBAR_THUMB_BOTTOM_IMG, tsrc));

			if (style.isNotDefault(StyleSpec::SCROLLBAR_THUMB_TOP_SIZE) &&
					style.isNotDefault(StyleSpec::SCROLLBAR_THUMB_BOTTOM_SIZE)) {
				// Clamp: cap sizes are untrusted formspec input; bound them so a
				// negative value can't invert the cap rect and a huge one can't
				// overdraw the thumb.
				m_slider_top_size = rangelim(
						style.getS32(StyleSpec::SCROLLBAR_THUMB_TOP_SIZE, 1), 0, 1024);
				m_slider_bottom_size = rangelim(
						style.getS32(StyleSpec::SCROLLBAR_THUMB_BOTTOM_SIZE, 1), 0, 1024);
			}
		}
		setTextures(textures);
	}
}

void GUIScrollBar::drawTexture(video::ITexture *texture, const core::rect<s32> &dest) const
{
	video::IVideoDriver *driver = Environment->getVideoDriver();
	if (!texture || !driver)
		return;
	const core::dimension2du ts = texture->getOriginalSize();
	// Filtered-scaled draw (same path as image[] / drawItemStack): with
	// gui_scaling_filter enabled the texture is pre-scaled on the CPU to the
	// exact dest size instead of GPU nearest-sampling, which drops/duplicates
	// texel rows of thin art at fractional scales. Falls back to a plain
	// draw2DImage when the setting is off.
	draw2DImageFilterScaled(driver, texture, dest,
			core::rect<s32>(0, 0, ts.Width, ts.Height),
			&AbsoluteClippingRect, nullptr, true);
}

void GUIScrollBar::draw()
{
	if (!IsVisible)
		return;

	// No custom textures → stock CGUIScrollBar rendering (full upstream look
	// and behavior). Custom textures only override the visuals below.
	if (m_textures.empty()) {
		CGUIScrollBar::draw();
		return;
	}

	IGUISkin *skin = Environment->getSkin();
	if (!skin)
		return;

	// Mirror CGUIScrollBar's icon-color-driven control refresh.
	const video::SColor icon_color = skin->getColor(
			isEnabled() ? EGDC_WINDOW_SYMBOL : EGDC_GRAY_WINDOW_SYMBOL);
	if (icon_color != CurrentIconColor)
		refreshControls();

	const core::rect<s32> track = AbsoluteRect;
	const s32 w = track.getWidth();
	const s32 h = track.getHeight();

	// SliderRect is the thumb rect CGUIScrollBar::OnEvent hit-tests for drag;
	// keep it in sync exactly as the stock draw() does, or the thumb becomes
	// undraggable. Defaults to the whole track when there is no range.
	SliderRect = track;

	// Track background (texture 0), inset by the bar's cross-axis size on the
	// main axis — the square end zones are left for the arrow buttons (shown)
	// or the end caps (hidden). Matches the 5.15.0 layout so the bg texture
	// scales over the same region (h - 2*w), not the full track.
	core::rect<s32> bg = track;
	if (Horizontal) {
		bg.UpperLeftCorner.X += h;
		bg.LowerRightCorner.X -= h;
	} else {
		bg.UpperLeftCorner.Y += w;
		bg.LowerRightCorner.Y -= w;
	}
	drawTexture(m_textures[0], bg);

	// Track end caps (textures 2,3) when the arrow buttons are hidden.
	if (m_textures.size() >= 4 && UpDownVisible == HIDE) {
		if (Horizontal) {
			drawTexture(m_textures[2], core::rect<s32>(track.UpperLeftCorner.X,
					track.UpperLeftCorner.Y, track.UpperLeftCorner.X + h,
					track.LowerRightCorner.Y));
			drawTexture(m_textures[3], core::rect<s32>(track.LowerRightCorner.X - h,
					track.UpperLeftCorner.Y, track.LowerRightCorner.X,
					track.LowerRightCorner.Y));
		} else {
			drawTexture(m_textures[2], core::rect<s32>(track.UpperLeftCorner.X,
					track.UpperLeftCorner.Y, track.LowerRightCorner.X,
					track.UpperLeftCorner.Y + w));
			drawTexture(m_textures[3], core::rect<s32>(track.UpperLeftCorner.X,
					track.LowerRightCorner.Y - w, track.LowerRightCorner.X,
					track.LowerRightCorner.Y));
		}
	}

	// Thumb (texture 1), optionally 3-part with end caps (4,5).
	if (core::isnotzero(range())) {
		if (Horizontal) {
			SliderRect.UpperLeftCorner.X = track.UpperLeftCorner.X + DrawPos - DrawHeight / 2;
			SliderRect.LowerRightCorner.X = SliderRect.UpperLeftCorner.X + DrawHeight;
		} else {
			SliderRect.UpperLeftCorner.Y = track.UpperLeftCorner.Y + DrawPos - DrawHeight / 2;
			SliderRect.LowerRightCorner.Y = SliderRect.UpperLeftCorner.Y + DrawHeight;
		}
		const core::rect<s32> thumb = SliderRect;

		if (m_textures.size() >= 6) {
			// Cap sizes: explicit style px when configured (> 0), otherwise
			// auto-match the bar's cross-axis width so square cap art stays
			// round at any client display scale — a fixed px value in the
			// style can't fit every DPI, since one formspec string is sent
			// to all clients. Clamped so the caps never overlap inside a
			// short thumb.
			const s32 cross = Horizontal ? h : w;
			const s32 thumb_len = Horizontal ? thumb.getWidth() : thumb.getHeight();
			s32 top_size = m_slider_top_size > 0 ? m_slider_top_size : cross;
			s32 bottom_size = m_slider_bottom_size > 0 ? m_slider_bottom_size : cross;
			top_size = std::min(top_size, thumb_len / 2);
			bottom_size = std::min(bottom_size, thumb_len - top_size);
			s32 mid = thumb_len - (top_size + bottom_size);
			if (mid <= 0)
				mid = 1;
			if (Horizontal) {
				core::rect<s32> a(thumb.UpperLeftCorner.X, thumb.UpperLeftCorner.Y,
						thumb.UpperLeftCorner.X + top_size, thumb.LowerRightCorner.Y);
				core::rect<s32> b(a.LowerRightCorner.X, thumb.UpperLeftCorner.Y,
						a.LowerRightCorner.X + mid, thumb.LowerRightCorner.Y);
				core::rect<s32> c(b.LowerRightCorner.X, thumb.UpperLeftCorner.Y,
						b.LowerRightCorner.X + bottom_size, thumb.LowerRightCorner.Y);
				drawTexture(m_textures[1], b);
				drawTexture(m_textures[4], a);
				drawTexture(m_textures[5], c);
			} else {
				core::rect<s32> a(thumb.UpperLeftCorner.X, thumb.UpperLeftCorner.Y,
						thumb.LowerRightCorner.X, thumb.UpperLeftCorner.Y + top_size);
				core::rect<s32> b(thumb.UpperLeftCorner.X, a.LowerRightCorner.Y,
						thumb.LowerRightCorner.X, a.LowerRightCorner.Y + mid);
				core::rect<s32> c(thumb.UpperLeftCorner.X, b.LowerRightCorner.Y,
						thumb.LowerRightCorner.X, b.LowerRightCorner.Y + bottom_size);
				drawTexture(m_textures[1], b);
				drawTexture(m_textures[4], a);
				drawTexture(m_textures[5], c);
			}
		} else {
			drawTexture(m_textures[1], thumb);
		}
	}

	// Arrow buttons (children) on top.
	IGUIElement::draw();
}
#endif
