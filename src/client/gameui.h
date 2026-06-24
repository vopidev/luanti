// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2018 nerzhul, Loic Blot <loic.blot@unix-experience.fr>

#pragma once

#include "irrlichttypes.h"
#include <IGUIEnvironment.h>
#include <memory>
#if IS_VOPI_ENGINE
#include "gui/guiNineSliceBackground.h"
#endif
#include "game.h"
#include "gui/statusTextHelper.h"


class Client;
class EnrichedString;
class GUIChatConsole;
struct MapDrawControl;
struct PointedThing;

/*
 * This object intend to contain the core UI elements
 * It includes:
 *   - status texts
 *   - debug texts
 *   - chat texts
 *   - hud flags
 */
class GameUI
{
	// Temporary between coding time to move things here
	friend class Game;

	// Permit unittests to access members directly
	friend class TestGameUI;

public:
	GameUI() = default;
	~GameUI() = default;

	// Flags that can, or may, change during main game loop
	struct Flags
	{
		bool show_chat = true;
		bool show_hud = true;
		bool show_minimal_debug = false;
		bool show_basic_debug = false;
		bool show_profiler_graph = false;
	};

	void init();
	void update(const RunStats &stats, Client *client, MapDrawControl *draw_control,
			const CameraOrientation &cam, const PointedThing &pointed_old,
			const GUIChatConsole *chat_console, float dtime);

	void initFlags();
	const Flags &getFlags() const { return m_flags; }

	inline void setInfoText(const std::wstring &str) { m_infotext = str; }
	inline void clearInfoText() { m_infotext.clear(); }

	inline void showStatusText(const std::wstring &str)
	{
		if (m_status_text)
			m_status_text->showStatusText(str);
	}
	void showTranslatedStatusText(const char *str);
	inline void clearStatusText()
	{
		if (m_status_text)
			m_status_text->clearStatusText();
	}

#if IS_VOPI_ENGINE
	// VOPI: anchor the status panel above the hotbar (y = HUD hotbar top edge),
	// or -1 to fall back to the screen-bottom offset. Set each frame from game.
	inline void setStatusHotbarAnchor(s32 y)
	{
		if (m_status_text)
			m_status_text->setHotbarAnchorY(y);
	}
#endif

	bool isChatVisible()
	{
		return m_flags.show_chat && m_recent_chat_count != 0 && m_profiler_current_page == 0;
	}
	void setChatText(const EnrichedString &chat_text, u32 recent_chat_count);
	void updateChatSize();

	void updateProfiler();

	void toggleChat(Client *client);
	void toggleHud();
	void toggleProfiler();

	void clearText();

private:
	Flags m_flags;

	float m_drawtime_avg = 0;

	gui::IGUIStaticText *m_guitext = nullptr;  // First line of debug text
	gui::IGUIStaticText *m_guitext2 = nullptr; // Second line of debug text

#if IS_VOPI_ENGINE && (defined(__ANDROID__) || defined(__IOS__))
	gui::IGUIStaticText *m_guitext3 = nullptr; // Third line of debug text
	gui::IGUIStaticText *m_guitext4 = nullptr; // Fourth line of debug text
#endif

	gui::IGUIStaticText *m_guitext_info = nullptr; // At the middle of the screen
	std::wstring m_infotext;

#if IS_VOPI_ENGINE
	f32 round_screen = 0;
	s32 button_size = 0;

	s32 m_info_text_margin_right = 0;
	s32 m_info_text_margin_top = 0;
#endif

	std::unique_ptr<StatusTextHelper> m_status_text = nullptr;

#if IS_VOPI_ENGINE
	NineSliceBackground m_status_bg;
	bool m_show_status_background = true;
	f32 m_status_text_bottom_offset = 0.25f;
#endif

	gui::IGUIStaticText *m_guitext_chat = nullptr; // Chat text
	u32 m_recent_chat_count = 0;
	core::rect<s32> m_current_chat_size{0, 0, 0, 0};

#if IS_VOPI_ENGINE
	NineSliceBackground m_chat_bg;
	bool m_show_chat_background = true;
#endif

	gui::IGUIStaticText *m_guitext_profiler = nullptr; // Profiler text
	u8 m_profiler_current_page = 0;
	u8 m_profiler_max_page = 1;
};
