// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2018 nerzhul, Loic Blot <loic.blot@unix-experience.fr>

#pragma once

#include "irrlichttypes.h"
#include <IGUIEnvironment.h>
#if IS_VOPI_ENGINE
#include <IGUIImage.h>
#endif
#include "game.h"


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
	GameUI();
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
		m_statustext = str;
		m_statustext_time = 0.0f;
	}
	void showTranslatedStatusText(const char *str);
	inline void clearStatusText() { m_statustext.clear(); }

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
	
	f32 round_screen = 0;
	s32 button_size = 0;
#endif

	gui::IGUIStaticText *m_guitext_info = nullptr; // At the middle of the screen
	std::wstring m_infotext;

#if IS_VOPI_ENGINE
	s32 m_info_text_margin_right;
	s32 m_info_text_margin_top;

	//Info text background
	gui::IGUIImage *m_guiimage_info_up_left = nullptr;
	gui::IGUIImage *m_guiimage_info_up = nullptr;
	gui::IGUIImage *m_guiimage_info_up_right = nullptr;

	gui::IGUIImage *m_guiimage_info_left = nullptr;
	gui::IGUIImage *m_guiimage_info_center = nullptr;
	gui::IGUIImage *m_guiimage_info_right = nullptr;

	gui::IGUIImage *m_guiimage_info_down_left = nullptr;
	gui::IGUIImage *m_guiimage_info_down = nullptr;
	gui::IGUIImage *m_guiimage_info_down_right = nullptr;
#endif

	gui::IGUIStaticText *m_guitext_status = nullptr;
	std::wstring m_statustext;
	float m_statustext_time = 0.0f;
	video::SColor m_statustext_initial_color;

#if IS_VOPI_ENGINE
	//Status text background
	gui::IGUIImage *m_guiimage_status_up_left = nullptr;
	gui::IGUIImage *m_guiimage_status_up = nullptr;
	gui::IGUIImage *m_guiimage_status_up_right = nullptr;

	gui::IGUIImage *m_guiimage_status_left = nullptr;
	gui::IGUIImage *m_guiimage_status_center = nullptr;
	gui::IGUIImage *m_guiimage_status_right = nullptr;

	gui::IGUIImage *m_guiimage_status_down_left = nullptr;
	gui::IGUIImage *m_guiimage_status_down = nullptr;
	gui::IGUIImage *m_guiimage_status_down_right = nullptr;

	bool m_show_status_background = true;
#endif

	gui::IGUIStaticText *m_guitext_chat = nullptr; // Chat text
	u32 m_recent_chat_count = 0;
	core::rect<s32> m_current_chat_size{0, 0, 0, 0};
	
#if IS_VOPI_ENGINE
	//Chat text background
	gui::IGUIImage *m_guiimage_chat_up_left = nullptr;
	gui::IGUIImage *m_guiimage_chat_up = nullptr;
	gui::IGUIImage *m_guiimage_chat_up_right = nullptr;
	
	gui::IGUIImage *m_guiimage_chat_left = nullptr;
	gui::IGUIImage *m_guiimage_chat_center = nullptr;
	gui::IGUIImage *m_guiimage_chat_right = nullptr;
	
	gui::IGUIImage *m_guiimage_chat_down_left = nullptr;
	gui::IGUIImage *m_guiimage_chat_down = nullptr;
	gui::IGUIImage *m_guiimage_chat_down_right = nullptr;
	
	bool m_show_chat_background = true;
#endif

	gui::IGUIStaticText *m_guitext_profiler = nullptr; // Profiler text
	u8 m_profiler_current_page = 0;
	const u8 m_profiler_max_page = 3;
};
