/*
Copyright (C) 2002-2013 Nikolaus Gebhardt
This file is part of the "Irrlicht Engine".
For conditions of distribution and use, see copyright notice in irrlicht.h
*/

#pragma once

#include <CGUIScrollBar.h>
#if IS_VOPI_ENGINE
#include "StyleSpec.h"
#include <vector>
#endif

class ISimpleTextureSource;

using namespace gui;

class GUIScrollBar final : public CGUIScrollBar
{
public:
	GUIScrollBar(IGUIEnvironment *environment, IGUIElement *parent, s32 id,
			core::rect<s32> rectangle, bool horizontal, ISimpleTextureSource *tsrc);

	virtual ~GUIScrollBar() {}

#if IS_VOPI_ENGINE
	// VOPI: custom multi-part scrollbar textures, drawn over the stock
	// CGUIScrollBar geometry — all upstream behavior (auto-scroll,
	// interpolation, events) is inherited; only the visuals change.
	// Texture order: [0]=track bg, [1]=thumb, [2]=track top cap,
	// [3]=track bottom cap, [4]=thumb top cap, [5]=thumb bottom cap.
	void setTextures(const std::vector<video::ITexture *> &textures);
	void setStyle(const StyleSpec &style, ISimpleTextureSource *tsrc);
	void draw() override;

	// Convenience bool overload (false = hide arrows); keeps the inherited
	// ArrowVisibility-enum overload accessible too.
	using CGUIScrollBar::setArrowsVisible;
	void setArrowsVisible(bool visible);

	// Expose the (protected) interactive scroll setter so touch drag-to-scroll
	// and momentum can move the bar exactly like a user drag, emitting
	// EGET_SCROLL_BAR_CHANGED (server notify + bound-container sync).
	using CGUIScrollBar::setPosAndSend;

	// Emit EGET_SCROLL_BAR_CHANGED for the current position without moving the
	// bar, to flush a value reached via the silent setPos() (used by fling
	// send-throttling) to listeners.
	void sendChanged()
	{
		if (Parent) {
			SEvent e;
			e.EventType = EET_GUI_EVENT;
			e.GUIEvent.Caller = this;
			e.GUIEvent.Element = nullptr;
			e.GUIEvent.EventType = EGET_SCROLL_BAR_CHANGED;
			Parent->OnEvent(e);
		}
	}

private:
	void drawTexture(video::ITexture *texture, const core::rect<s32> &dest) const;

	std::vector<video::ITexture *> m_textures;
	s32 m_slider_top_size = 0;
	s32 m_slider_bottom_size = 0;
#endif
};
