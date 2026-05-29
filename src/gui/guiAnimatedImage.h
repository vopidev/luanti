#pragma once

#include <algorithm>
#include <IGUIElement.h>
#include <IGUIEnvironment.h>



class GUIAnimatedImage : public gui::IGUIElement {
public:
	GUIAnimatedImage(gui::IGUIEnvironment *env, gui::IGUIElement *parent,
		s32 id, const core::rect<s32> &rectangle);

	virtual void draw() override;

	void setTexture(video::ITexture *texture) { m_texture = texture; };
	video::ITexture *getTexture() const { return m_texture; };

	void setMiddleRect(const core::rect<s32> &middle) { m_middle = middle; };
	core::rect<s32> getMiddleRect() const { return m_middle; };

	void setFrameDuration(u64 duration) { m_frame_duration = duration; };
	u64 getFrameDuration() const { return m_frame_duration; };

	void setFrameCount(s32 count) { m_frame_count = std::max(count, 1); };
	s32 getFrameCount() const { return m_frame_count; };

	void setFrameIndex(s32 frame) { m_frame_idx = std::max(frame, 0); };
	s32 getFrameIndex() const { return m_frame_idx; };

	// When false, the animation plays once and holds on the last frame
	// instead of looping back to frame 1. Default true (upstream behaviour).
	void setLoop(bool loop) { m_loop = loop; };
	bool getLoop() const { return m_loop; };

private:
	video::ITexture *m_texture = nullptr;

	u64 m_global_time = 0;
	s32 m_frame_idx = 0;
	s32 m_frame_count = 1;
	u64 m_frame_duration = 0;
	u64 m_frame_time = 0;
	bool m_loop = true;

	core::rect<s32> m_middle;
};
