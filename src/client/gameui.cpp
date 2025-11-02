// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2018 nerzhul, Loic Blot <loic.blot@unix-experience.fr>

#include "gameui.h"
#include <irrlicht_changes/static_text.h>
#include <gettext.h>
#include "gui/mainmenumanager.h"
#include "gui/guiChatConsole.h"
#include "gui/touchcontrols.h"
#include "util/enriched_string.h"
#include "util/pointedthing.h"
#include "client.h"
#include "clientmap.h"
#include "fontengine.h"
#include "hud.h" // HUD_FLAG_*
#include "nodedef.h"
#include "profiler.h"
#include "renderingengine.h"
#include "version.h"

inline static const char *yawToDirectionString(int yaw)
{
	static const char *direction[4] =
		{"North +Z", "West -X", "South -Z", "East +X"};

	yaw = wrapDegrees_0_360(yaw);
	yaw = (yaw + 45) % 360 / 90;

	return direction[yaw];
}

GameUI::GameUI()
{
	if (guienv && guienv->getSkin())
		m_statustext_initial_color = guienv->getSkin()->getColor(gui::EGDC_BUTTON_TEXT);
	else
		m_statustext_initial_color = video::SColor(255, 0, 0, 0);

}
void GameUI::init()
{
	// First line of debug text
	m_guitext = gui::StaticText::add(guienv, utf8_to_wide(PROJECT_NAME_C).c_str(),
		core::rect<s32>(0, 0, 0, 0), false, true, guiroot);

	// Second line of debug text
	m_guitext2 = gui::StaticText::add(guienv, L"", core::rect<s32>(0, 0, 0, 0), false,
		true, guiroot);
	
#if IS_VOPI_ENGINE && (defined(__ANDROID__) || defined(__IOS__))
	// Third line of debug text
	m_guitext3 = gui::StaticText::add(guienv, L"", core::rect<s32>(0, 0, 0, 0), false,
		false, guiroot);
	// Fourth line of debug text
	m_guitext4 = gui::StaticText::add(guienv, L"", core::rect<s32>(0, 0, 0, 0), false,
		false, guiroot);
	round_screen = g_settings->getFloat("hud_round_screen");
	v2u32 screensize = RenderingEngine::getWindowSize();
	button_size = (MYMIN(screensize.Y / 4.5f,
				RenderingEngine::getDisplayDensity() *
				g_settings->getFloat("hud_scaling") * 65.0f)) * 1.3f;
#endif

	// Chat text
	m_guitext_chat = gui::StaticText::add(guienv, L"", core::rect<s32>(0, 0, 0, 0),
		//false, false); // Disable word wrap as of now
		false, true, guiroot);
	u16 chat_font_size = g_settings->getU16("chat_font_size");
	if (chat_font_size != 0) {
		m_guitext_chat->setOverrideFont(g_fontengine->getFont(
			rangelim(chat_font_size, 5, 72), FM_Unspecified));
	}


	// Infotext of nodes and objects.
	// If in debug mode, object debug infos shown here, too.
	// Located on the left on the screen, below chat.
	u32 chat_font_height = m_guitext_chat->getActiveFont()->getDimension(L"Ay").Height;
	m_guitext_info = gui::StaticText::add(guienv, L"",
		// Size is limited; text will be truncated after 6 lines.
		core::rect<s32>(0, 0, 400, g_fontengine->getTextHeight() * 6) +
			v2s32(100, chat_font_height *
			(g_settings->getU16("recent_chat_messages") + 3)),
			false, true, guiroot);

	// Status text (displays info when showing and hiding GUI stuff, etc.)
	m_guitext_status = gui::StaticText::add(guienv, L"<Status>",
		core::rect<s32>(0, 0, 0, 0), false, false, guiroot);
	m_guitext_status->setVisible(false);

	// Profiler text (size is updated when text is updated)
	m_guitext_profiler = gui::StaticText::add(guienv, L"<Profiler>",
		core::rect<s32>(0, 0, 0, 0), false, false, guiroot);
	m_guitext_profiler->setOverrideFont(g_fontengine->getFont(
		g_fontengine->getDefaultFontSize() * 0.9f, FM_Mono));
	m_guitext_profiler->setVisible(false);
}

void GameUI::update(const RunStats &stats, Client *client, MapDrawControl *draw_control,
	const CameraOrientation &cam, const PointedThing &pointed_old,
	const GUIChatConsole *chat_console, float dtime)
{
	v2u32 screensize = RenderingEngine::getWindowSize();

	LocalPlayer *player = client->getEnv().getLocalPlayer();
	
#if IS_VOPI_ENGINE && (defined(__ANDROID__) || defined(__IOS__))
	v3f player_position = player->getPosition();
	
	// Minimal debug text must only contain info that can't give a gameplay advantage
	if (m_flags.show_minimal_debug) {
		const u16 fps = 1.0 / stats.dtime_jitter.avg;
		m_drawtime_avg *= 0.95f;
		m_drawtime_avg += 0.05f * (stats.drawtime / 1000);
		
		std::ostringstream os1(std::ios_base::binary);
		os1 << std::fixed
			<< "| FPS: " << fps
			<< std::setprecision(0)
			<< " | dt: " << m_drawtime_avg << " ms"
			<< " | yaw: "
			<< (wrapDegrees_0_360(cam.camera_yaw)) << "°"
			<< " |";
		
		setStaticText(m_guitext, utf8_to_wide(os1.str()).c_str());
		
		std::ostringstream os2(std::ios_base::binary);
		os2 << std::fixed
			<< "| X: " << static_cast<int>(player_position.X / BS)
			<< ", Y: " << static_cast<int>(player_position.Y / BS)
			<< ", Z: " << static_cast<int>(player_position.Z / BS)
			<< " |";
		
		setStaticText(m_guitext2, utf8_to_wide(os2.str()).c_str());
		
		m_guitext->setRelativePosition(core::rect<s32>(screensize.X/2 + button_size,
			5, screensize.X, 5 + g_fontengine->getTextHeight()));
		
		m_guitext2->setRelativePosition(core::rect<s32>(screensize.X/2 + button_size,
			5 + g_fontengine->getTextHeight(), screensize.X,
			5 + g_fontengine->getTextHeight() * 2
		));
		
		m_guitext->setBackgroundColor(video::SColor(85,0,0,0));
		m_guitext2->setBackgroundColor(video::SColor(85,0,0,0));
	}
	
	// Finally set the guitext visible depending on the flag
	m_guitext->setVisible(m_flags.show_minimal_debug);
	m_guitext2->setVisible(m_flags.show_minimal_debug);
	// Basic debug text also shows info that might give a gameplay advantage
	if (m_flags.show_basic_debug) {
		LocalPlayer *player = client->getEnv().getLocalPlayer();
		v3f player_position = player->getPosition();
		std::ostringstream os1(std::ios_base::binary);
		os1 << std::setprecision(1) << std::fixed
			<< "| seed: "
			<< ((u64)client->getMapSeed())
			<< " |";
		
		setStaticText(m_guitext3, utf8_to_wide(os1.str()).c_str());
		
		std::ostringstream os2(std::ios_base::binary);
		if (pointed_old.type == POINTEDTHING_NODE) {
			ClientMap &map = client->getEnv().getClientMap();
			const NodeDefManager *nodedef = client->getNodeDefManager();
			MapNode n = map.getNode(pointed_old.node_undersurface);

			if (n.getContent() != CONTENT_IGNORE) {
				if (nodedef->get(n).name == "unknown") {
					os2 << "| pointed: <unknown node> |";
				} else {
					os2 << std::fixed <<"| pointed: " << nodedef->get(n).name << " |";
				}
				//os2 << " param2: " << (u64) n.getParam2()
				//	<< " |";
			}
		}
		setStaticText(m_guitext4, utf8_to_wide(os2.str()).c_str());
		m_guitext3->setRelativePosition(core::rect<s32>(screensize.X/2 + button_size,
			5 + g_fontengine->getTextHeight() * 2, screensize.X,
			5 + g_fontengine->getTextHeight() * 3
		));
		
		m_guitext4->setRelativePosition(core::rect<s32>(screensize.X/2 + button_size,
			5 + g_fontengine->getTextHeight() * 3, screensize.X,
			5 + g_fontengine->getTextHeight() * 4
		));
		
		m_guitext3->setBackgroundColor(video::SColor(85,0,0,0));
		
		if (pointed_old.type == POINTEDTHING_NODE) {
			ClientMap &map = client->getEnv().getClientMap();
			const NodeDefManager *nodedef = client->getNodeDefManager();
			MapNode n = map.getNode(pointed_old.node_undersurface);
			if (n.getContent() != CONTENT_IGNORE) {
				m_guitext4->setBackgroundColor(video::SColor(85,0,0,0));
			}
		} else {
			m_guitext4->setBackgroundColor(video::SColor(0,0,0,0));
		}
	}
	m_guitext3->setVisible(m_flags.show_basic_debug);
	m_guitext4->setVisible(m_flags.show_basic_debug);
	setStaticText(m_guitext_info, m_infotext.c_str());
	m_guitext_info->setVisible(m_flags.show_hud && g_menumgr.menuCount() == 0);
#else

	s32 minimal_debug_height = 0;

	// Minimal debug text must only contain info that can't give a gameplay advantage
	if (m_flags.show_minimal_debug) {
		const u16 fps = 1.0 / stats.dtime_jitter.avg;
		m_drawtime_avg *= 0.95f;
		m_drawtime_avg += 0.05f * (stats.drawtime / 1000);

		std::ostringstream os(std::ios_base::binary);
		os << std::fixed
			<< PROJECT_NAME_C " " << g_version_hash
			<< " | FPS: " << fps
			<< std::setprecision(fps >= 100 ? 1 : 0)
			<< " | drawtime: " << m_drawtime_avg << "ms"
			<< std::setprecision(1)
			<< " | dtime jitter: "
			<< (stats.dtime_jitter.max_fraction * 100.0f) << "%"
			<< std::setprecision(1)
			<< " | view range: "
			<< (draw_control->range_all ? "All" : itos(draw_control->wanted_range))
			<< std::setprecision(2)
			<< " | RTT: " << (client->getRTT() * 1000.0f) << "ms";

		m_guitext->setRelativePosition(core::rect<s32>(5, 5, screensize.X, screensize.Y));

		setStaticText(m_guitext, utf8_to_wide(os.str()).c_str());

		minimal_debug_height = m_guitext->getTextHeight();
	}

	// Finally set the guitext visible depending on the flag
	m_guitext->setVisible(m_flags.show_minimal_debug);

	// Basic debug text also shows info that might give a gameplay advantage
	if (m_flags.show_basic_debug) {
		v3f player_position = player->getPosition();

		std::ostringstream os(std::ios_base::binary);
		os << std::setprecision(1) << std::fixed
			<< "pos: (" << (player_position.X / BS)
			<< ", " << (player_position.Y / BS)
			<< ", " << (player_position.Z / BS)
			<< ") | yaw: " << (wrapDegrees_0_360(cam.camera_yaw)) << "° "
			<< yawToDirectionString(cam.camera_yaw)
			<< " | pitch: " << (-wrapDegrees_180(cam.camera_pitch)) << "°"
			<< " | seed: " << ((u64)client->getMapSeed());

		if (pointed_old.type == POINTEDTHING_NODE) {
			ClientMap &map = client->getEnv().getClientMap();
			const NodeDefManager *nodedef = client->getNodeDefManager();
			MapNode n = map.getNode(pointed_old.node_undersurface);

			if (n.getContent() != CONTENT_IGNORE) {
				if (nodedef->get(n).name == "unknown") {
					os << ", pointed: <unknown node>";
				} else {
					os << ", pointed: " << nodedef->get(n).name;
				}
				os << ", param2: " << (u64) n.getParam2();
			}
		}

		m_guitext2->setRelativePosition(core::rect<s32>(5, 5 + minimal_debug_height,
				screensize.X, screensize.Y));

		setStaticText(m_guitext2, utf8_to_wide(os.str()).c_str());
	}

	m_guitext2->setVisible(m_flags.show_basic_debug);

	setStaticText(m_guitext_info, m_infotext.c_str());
	m_guitext_info->setVisible(m_flags.show_hud && g_menumgr.menuCount() == 0);
#endif

	static const float statustext_time_max = 1.5f;

	if (!m_statustext.empty()) {
		m_statustext_time += dtime;

		if (m_statustext_time >= statustext_time_max) {
			clearStatusText();
			m_statustext_time = 0.0f;
		}
	}

	IGUIStaticText *guitext_status;
	bool overriden = g_touchcontrols && g_touchcontrols->isStatusTextOverriden();
	if (overriden) {
		guitext_status = g_touchcontrols->getStatusText();
		m_guitext_status->setVisible(false);
	} else {
		guitext_status = m_guitext_status;
		if (g_touchcontrols)
			g_touchcontrols->getStatusText()->setVisible(false);
	}

	setStaticText(guitext_status, m_statustext.c_str());
	guitext_status->setVisible(!m_statustext.empty());

	if (!m_statustext.empty()) {
		s32 status_width  = guitext_status->getTextWidth();
		s32 status_height = guitext_status->getTextHeight();
#if IS_VOPI_ENGINE && (defined(__ANDROID__) || defined(__IOS__))
		s32 status_y = screensize.Y  - 250;
#else
		s32 status_y = screensize.Y  - (overriden ? 15 : 150);
#endif
		s32 status_x = (screensize.X - status_width) / 2;

		guitext_status->setRelativePosition(core::rect<s32>(status_x ,
			status_y - status_height, status_x + status_width, status_y));

#if IS_VOPI_ENGINE && (defined(__ANDROID__) || defined(__IOS__))
		m_guitext_status->setBackgroundColor(video::SColor(85,0,0,0));
		//m_guitext_status->setDrawBorder(true);
#endif

		// Fade out
		video::SColor fade_color = m_statustext_initial_color;
		f32 d = m_statustext_time / statustext_time_max;
		fade_color.setAlpha(static_cast<u32>(
			fade_color.getAlpha() * (1.0f - d * d)));
		guitext_status->setOverrideColor(fade_color);
#if !IS_VOPI_ENGINE  && !defined(__ANDROID__) && !defined(__IOS__)
		guitext_status->enableOverrideColor(true);
#endif
	}

#if IS_VOPI_ENGINE  && (defined(__ANDROID__) || defined(__IOS__))
	m_guitext_chat->setBackgroundColor(video::SColor(85,0,0,0));
#endif

	// Hide chat when disabled by server or when console is visible
	m_guitext_chat->setVisible(isChatVisible() && !chat_console->isVisible() && (player->hud_flags & HUD_FLAG_CHAT_VISIBLE));
}

void GameUI::initFlags()
{
	m_flags = GameUI::Flags();
}

void GameUI::showTranslatedStatusText(const char *str)
{
	showStatusText(wstrgettext(str));
}

void GameUI::setChatText(const EnrichedString &chat_text, u32 recent_chat_count)
{
	setStaticText(m_guitext_chat, chat_text);

	m_recent_chat_count = recent_chat_count;
}

void GameUI::updateChatSize()
{
	// Update gui element size and position
	s32 chat_y = 5;

#if IS_VOPI_ENGINE && (defined(__ANDROID__) || defined(__IOS__))
	chat_y = g_fontengine->getLineHeight() * 2;
#else
	if (m_flags.show_minimal_debug)
		chat_y += m_guitext->getTextHeight();
	if (m_flags.show_basic_debug)
		chat_y += m_guitext2->getTextHeight();
#endif

	const v2u32 &window_size = RenderingEngine::getWindowSize();

#if IS_VOPI_ENGINE && (defined(__ANDROID__) || defined(__IOS__))
	core::rect<s32> chat_size(round_screen, chat_y, window_size.X * 0.43f, 0);
	chat_size.LowerRightCorner.Y = std::min((s32)window_size.Y, m_guitext_chat->getTextHeight() + chat_y);
#else
	core::rect<s32> chat_size(10, chat_y, window_size.X - 20, 0);
	chat_size.LowerRightCorner.Y = std::min((s32)window_size.Y,
			m_guitext_chat->getTextHeight() + chat_y);
#endif

	if (chat_size == m_current_chat_size)
		return;
	m_current_chat_size = chat_size;

	m_guitext_chat->setRelativePosition(chat_size);
}

void GameUI::updateProfiler()
{
	if (m_profiler_current_page != 0) {
		std::ostringstream os(std::ios_base::binary);
		os << "   Profiler page " << (int)m_profiler_current_page <<
				", elapsed: " << g_profiler->getElapsedMs() << " ms)" << std::endl;

		g_profiler->print(os, m_profiler_current_page, m_profiler_max_page);

		EnrichedString str(utf8_to_wide(os.str()));
		str.setBackground(video::SColor(120, 0, 0, 0));
		setStaticText(m_guitext_profiler, str);

		core::dimension2d<u32> size = m_guitext_profiler->getOverrideFont()->
				getDimension(str.c_str());
		core::position2di upper_left(6, m_guitext->getTextHeight() * 2.5f);
		core::position2di lower_right = upper_left;
		lower_right.X += size.Width + 10;
		lower_right.Y += size.Height;

		m_guitext_profiler->setRelativePosition(core::rect<s32>(upper_left, lower_right));
	}

	m_guitext_profiler->setVisible(m_profiler_current_page != 0);
}

void GameUI::toggleChat(Client *client)
{
	if (client->getEnv().getLocalPlayer()->hud_flags & HUD_FLAG_CHAT_VISIBLE) {
		m_flags.show_chat = !m_flags.show_chat;
		if (m_flags.show_chat)
			showTranslatedStatusText("Chat shown");
		else
			showTranslatedStatusText("Chat hidden");
	} else {
		showTranslatedStatusText("Chat currently disabled by game or mod");
	}

}

void GameUI::toggleHud()
{
	m_flags.show_hud = !m_flags.show_hud;
	if (m_flags.show_hud)
		showTranslatedStatusText("HUD shown");
	else
		showTranslatedStatusText("HUD hidden");
}

void GameUI::toggleProfiler()
{
	m_profiler_current_page = (m_profiler_current_page + 1) % (m_profiler_max_page + 1);

	// FIXME: This updates the profiler with incomplete values
	updateProfiler();

	if (m_profiler_current_page != 0) {
		std::wstring msg = fwgettext("Profiler shown (page %d of %d)",
				m_profiler_current_page, m_profiler_max_page);
		showStatusText(msg);
	} else {
		showTranslatedStatusText("Profiler hidden");
	}
}

void GameUI::clearText()
{
	if (m_guitext_chat) {
		m_guitext_chat->remove();
		m_guitext_chat = nullptr;
	}

	if (m_guitext) {
		m_guitext->remove();
		m_guitext = nullptr;
	}

	if (m_guitext2) {
		m_guitext2->remove();
		m_guitext2 = nullptr;
	}
	
#if IS_VOPI_ENGINE && (defined(__ANDROID__) || defined(__IOS__))
	if (m_guitext3) {
		m_guitext3->remove();
		m_guitext3 = nullptr;
	}
	
	if (m_guitext4) {
		m_guitext4->remove();
		m_guitext4 = nullptr;
	}
#endif

	if (m_guitext_info) {
		m_guitext_info->remove();
		m_guitext_info = nullptr;
	}

	if (m_guitext_status) {
		m_guitext_status->remove();
		m_guitext_status = nullptr;
	}

	if (m_guitext_profiler) {
		m_guitext_profiler->remove();
		m_guitext_profiler = nullptr;
	}
}
