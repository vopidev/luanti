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
#include "hud_element.h" // HUD_FLAG_*
#include "nodedef.h"
#include "localplayer.h"
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
#if IS_VOPI_ENGINE
	video::IVideoDriver *driver = RenderingEngine::get_video_driver();
	// Get the base font size
	u16 base_font_size = g_fontengine->getDefaultFontSize();
	std::string textures_path = porting::path_share + "/textures/base/pack/gui_pop_up/";

	round_screen = g_settings->getFloat("round_screen");
	v2u32 screensize = driver->getScreenSize();
	button_size = (MYMIN(screensize.Y / 4.5f,
				RenderingEngine::getDisplayDensity() *
				g_settings->getFloat("hud_scaling") * 65.0f)) * 1.3f;
#endif
	// First line of debug text
	m_guitext = gui::StaticText::add(guienv, utf8_to_wide(PROJECT_NAME_C).c_str(),
		core::recti(), false, true, guiroot);

	// Second line of debug text
	m_guitext2 = gui::StaticText::add(guienv, L"", core::recti(), false,
		true, guiroot);

#if IS_VOPI_ENGINE && (defined(__ANDROID__) || defined(__IOS__))
	// Third line of debug text
	m_guitext3 = gui::StaticText::add(guienv, L"", core::rect<s32>(0, 0, 0, 0), false,
		false, guiroot);
	// Fourth line of debug text
	m_guitext4 = gui::StaticText::add(guienv, L"", core::rect<s32>(0, 0, 0, 0), false,
		false, guiroot);
#endif

	// Chat text
#if IS_VOPI_ENGINE
	m_guitext_chat = gui::StaticText::add(guienv, L"", core::rect<s32>(0, 0, 0, 0), false, true, guiroot);
	// Get the scaling factor from settings
	const f32 chat_font_scale = g_settings->getFloat("chat_font_scale");
	// Calculate the new font size and round it to the nearest integer
	const u16 chat_font_size = std::round(base_font_size * chat_font_scale);
#else
	m_guitext_chat = gui::StaticText::add(guienv, L"", core::recti(),
		false, true, guiroot);
	u16 chat_font_size = g_settings->getU16("chat_font_size");
#endif
	if (chat_font_size != 0) {
		m_guitext_chat->setOverrideFont(g_fontengine->getFont(
			rangelim(chat_font_size, 5, 72), FM_Unspecified));
	}

#if IS_VOPI_ENGINE
	m_chat_bg.init(guienv, driver, guiroot, textures_path, "gui_chat_bg");
	m_show_chat_background = g_settings->getBool("show_chat_background");
#endif

	// Infotext of nodes and objects.
#if IS_VOPI_ENGINE
	m_guitext_info = gui::StaticText::add(guienv, L"", core::rect<s32>(0, 0, 0, 0), false, false, guiroot);
	// Get the scaling factor from settings
	const f32 info_font_scale = g_settings->getFloat("info_font_scale");
	// Calculate the new font size and round it to the nearest integer
	const u16 info_font_size = std::round(base_font_size * info_font_scale);
	if (info_font_size != 0) {
		m_guitext_info->setOverrideFont(g_fontengine->getFont(
			rangelim(info_font_size, 5, 72), FM_Unspecified));
	}
	m_guitext_info->setTextAlignment(gui::EGUIA_CENTER, gui::EGUIA_CENTER);
	m_guitext_info->setVisible(false);

#if defined(__ANDROID__) || defined(__IOS__)
	m_info_text_margin_right = round_screen + 30;
	m_info_text_margin_top = g_fontengine->getLineHeight() * 2;
#else
	m_info_text_margin_right = 10;
	m_info_text_margin_top = 0;
#endif
#else
	// If in debug mode, object debug infos shown here, too.
	// Located on the left on the screen, below chat.
	u32 chat_font_height = m_guitext_chat->getActiveFont()->getDimension(L"Ay").Height;
	m_guitext_info = gui::StaticText::add(guienv, L"",
		// Size is limited; text will be truncated after 6 lines.
		core::rect<s32>(0, 0, 400, g_fontengine->getTextHeight() * 6) +
			v2s32(100, chat_font_height *
			(g_settings->getU16("recent_chat_messages") + 3)),
			false, true, guiroot);
#endif

	// Status text (displays info when showing and hiding GUI stuff, etc.)
#if IS_VOPI_ENGINE
	m_guitext_status = gui::StaticText::add(guienv, L"<Status>", core::rect<s32>(0, 0, 0, 0), false, false, guiroot);
	// Get the scaling factor from settings
	const f32 status_font_scale = g_settings->getFloat("status_font_scale");
	// Calculate the new font size and round it to the nearest integer
	const u16 status_font_size = std::round(base_font_size * status_font_scale);
	if (status_font_size != 0) {
		m_guitext_status->setOverrideFont(g_fontengine->getFont(
			rangelim(status_font_size, 5, 72), FM_Unspecified));
	}
	m_guitext_status->setTextAlignment(gui::EGUIA_CENTER, gui::EGUIA_CENTER);
#else
	m_guitext_status = gui::StaticText::add(guienv, L"<Status>",
		core::recti(), false, false, guiroot);
#endif
	m_guitext_status->setVisible(false);

#if IS_VOPI_ENGINE
	m_status_bg.init(guienv, driver, guiroot, textures_path, "gui_status_bg");
	m_show_status_background = g_settings->getBool("show_status_background");
	m_status_text_bottom_offset = g_settings->getFloat("status_text_bottom_offset");
#endif

	// Profiler text (size is updated when text is updated)
	m_guitext_profiler = gui::StaticText::add(guienv, L"<Profiler>",
		core::recti(), false, false, guiroot);
	m_guitext_profiler->setOverrideFont(g_fontengine->getFont(
		g_fontengine->getDefaultFontSize() * 0.9f, FM_Mono));
	m_guitext_profiler->setVisible(false);

#if IS_VOPI_ENGINE
	guiroot->bringToFront(m_guitext_chat);
	guiroot->bringToFront(m_guitext_info);
	guiroot->bringToFront(m_guitext_status);
#endif
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
			<< (wrapDegrees_0_360(cam.camera_yaw)) << "\xC2\xB0"
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
		const u16 fps = 1.0f / stats.dtime_jitter.avg;
		m_drawtime_avg *= 0.95f;
		m_drawtime_avg += 0.05f * (stats.drawtime / 1000);

		std::ostringstream os(std::ios_base::binary);
		os << std::fixed
			<< PROJECT_NAME_C " " << g_version_hash
			<< " | FPS: " << fps
			<< std::setprecision(m_drawtime_avg < 10 ? 1 : 0)
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

		setStaticText(m_guitext, utf8_to_wide(os.str()));

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

	//Status text
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
#if !IS_VOPI_ENGINE
		if (g_touchcontrols)
			g_touchcontrols->getStatusText()->setVisible(false);
#endif
	}

	setStaticText(guitext_status, m_statustext.c_str());
	guitext_status->setVisible(!m_statustext.empty());

#if IS_VOPI_ENGINE
	m_status_bg.setVisible(m_show_status_background && !m_statustext.empty());
#endif

	if (!m_statustext.empty()) {
#if IS_VOPI_ENGINE
		// Get screen dimensions
		v2u32 screensize = RenderingEngine::getWindowSize();
		// Calculate text dimensions
		s32 text_width = m_guitext_status->getTextWidth();
		s32 text_height = m_guitext_status->getTextHeight();
		// Define platform-specific variables
#if defined(__ANDROID__) || defined(__IOS__)
		s32 padding = 10;  // Padding for mobile
#else
		s32 padding = 5;  // Padding for desktop
#endif
		// Calculate central block height
		s32 central_height = text_height + (2 * padding);
		// Calculate side element dimensions
		s32 side_width = central_height / 4;
		// Ensure side_width is at least 5 pixels and at most 50 pixels
		side_width = std::max(5, std::min(side_width, 50));
		// Calculate total width and height
		s32 total_width = text_width + (side_width * 2) + (2 * padding);
		s32 total_height = central_height;
		// Ensure the width doesn't exceed the screen width
		total_width = std::min(total_width, (s32)screensize.X);
		// Calculate positions
		s32 status_y = screensize.Y - (s32)(screensize.Y * m_status_text_bottom_offset);
		s32 status_x = (screensize.X - total_width) / 2;
		// Position the text
		m_guitext_status->setRelativePosition(
			core::rect<s32>(
				status_x + side_width,
				status_y + padding,
				status_x + total_width - side_width,
				status_y + total_height - padding
			)
		);
		m_status_bg.setPosition(
			core::rect<s32>(status_x, status_y, status_x + total_width, status_y + total_height),
			side_width);
	}
#else
		s32 status_width  = guitext_status->getTextWidth();
		s32 status_height = guitext_status->getTextHeight();
		s32 status_y = screensize.Y  - (overriden ? 15 : 150);
		s32 status_x = (screensize.X - status_width) / 2;

		guitext_status->setRelativePosition(core::rect<s32>(status_x ,
			status_y - status_height, status_x + status_width, status_y));

#if defined(__ANDROID__) || defined(__IOS__)
		m_guitext_status->setBackgroundColor(video::SColor(85,0,0,0));
		//m_guitext_status->setDrawBorder(true);
#endif

		// Fade out
		video::SColor fade_color = m_statustext_initial_color;
		f32 d = m_statustext_time / statustext_time_max;
		fade_color.setAlpha(static_cast<u32>(
			fade_color.getAlpha() * (1.0f - d * d)));
		guitext_status->setOverrideColor(fade_color);
#if !defined(__ANDROID__) && !defined(__IOS__)
		guitext_status->enableOverrideColor(true);
#endif
	}

#if defined(__ANDROID__) || defined(__IOS__)
	m_guitext_chat->setBackgroundColor(video::SColor(85,0,0,0));
#endif
#endif

	//Chat text
	// Hide chat when disabled by server or when console is visible
	m_guitext_chat->setVisible(isChatVisible() && !chat_console->isVisible() && (player->hud_flags & HUD_FLAG_CHAT_VISIBLE));

#if IS_VOPI_ENGINE
	m_chat_bg.setVisible(isChatVisible() && !chat_console->isVisible() && m_show_chat_background && (player->hud_flags & HUD_FLAG_CHAT_VISIBLE));
#endif
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

#if IS_VOPI_ENGINE
	m_guitext_chat->enableOverrideColor(true);
	if (m_show_chat_background) {
		m_guitext_chat->setOverrideColor(video::SColor(255,251,158,185));
	} else {
		m_guitext_chat->setOverrideColor(video::SColor(255,255,255,255));
	}
#endif

	m_recent_chat_count = recent_chat_count;
}

void GameUI::updateChatSize()
{
#if IS_VOPI_ENGINE
	const v2u32& window_size = RenderingEngine::getWindowSize();
	// Step 1: Define chat window dimensions
	s32 chat_width = (window_size.X / 2) - round_screen - (button_size * 1.3);
	s32 max_chat_height = window_size.Y / 2;
	s32 min_chat_height = g_fontengine->getLineHeight();
	// Step 2: Determine the initial position of the chat
	s32 chat_y = 2;
	#if defined(__ANDROID__) || defined(__IOS__)
		chat_y = g_fontengine->getLineHeight() * 2;
	#else
		// Check if debug window is visible
		bool debug_visible = m_flags.show_minimal_debug || m_flags.show_basic_debug;
		if (debug_visible) {
			chat_y += g_fontengine->getTextHeight() * 2 + 5;  // 2 lines of debug text + margin
		}
	#endif
	// Step 3: Calculate the actual chat size based on content
	s32 content_height = m_guitext_chat->getTextHeight();
	s32 chat_height = std::max(min_chat_height, std::min(content_height, max_chat_height));
	// Step 4: Define the background size and position
	s32 padding;
	s32 corner_size;
	#if defined(__ANDROID__) || defined(__IOS__)
		padding = 18;
		corner_size = 15;
	#else
		padding = 9;
		corner_size = 8;
	#endif
	core::rect<s32> bg_size(
		#if defined(__ANDROID__) || defined(__IOS__)
			round_screen + 10,
		#else
			10,
		#endif
		chat_y,
		#if defined(__ANDROID__) || defined(__IOS__)
			round_screen + 10 + chat_width + padding * 2,
		#else
			10 + chat_width + padding * 2,
		#endif
		chat_y + chat_height + padding * 2
	);
	// Step 5: Define the chat text position
	core::rect<s32> chat_size = bg_size;
	chat_size.UpperLeftCorner.X += padding;
	chat_size.UpperLeftCorner.Y += padding;
	chat_size.LowerRightCorner.X -= padding;
	chat_size.LowerRightCorner.Y -= padding;
#else
	// Update gui element size and position
	s32 chat_y = 5;

	if (m_flags.show_minimal_debug)
		chat_y += m_guitext->getTextHeight();
	if (m_flags.show_basic_debug)
		chat_y += m_guitext2->getTextHeight();

	const v2u32 &window_size = RenderingEngine::getWindowSize();

	core::rect<s32> chat_size(10, chat_y, window_size.X - 20, 0);
	chat_size.LowerRightCorner.Y = std::min((s32)window_size.Y,
			m_guitext_chat->getTextHeight() + chat_y);
#endif

	// Check if the size has changed
	if (chat_size == m_current_chat_size)
		return;
	m_current_chat_size = chat_size;

	// Set the position of chat text
	m_guitext_chat->setRelativePosition(chat_size);

#if IS_VOPI_ENGINE
	m_chat_bg.setPosition(bg_size, corner_size);
#endif
}

void GameUI::updateProfiler()
{
	m_guitext_profiler->setVisible(m_profiler_current_page != 0);
	if (m_profiler_current_page == 0)
		return;

	std::ostringstream oss(std::ios_base::binary);
	oss << "Profiler page " << (int)m_profiler_current_page
		<< "/" << (int)m_profiler_max_page
		<< ", elapsed: " << g_profiler->getElapsedMs() << " ms" << std::endl;
	g_profiler->print(oss, m_profiler_current_page, m_profiler_max_page);

	EnrichedString str(utf8_to_wide(oss.str()));
	str.setBackground(video::SColor(120, 0, 0, 0));
	setStaticText(m_guitext_profiler, str);

	v2s32 upper_left(5, 10);
	if (m_flags.show_minimal_debug)
		upper_left.Y += m_guitext->getTextHeight();
	if (m_flags.show_basic_debug)
		upper_left.Y += m_guitext2->getTextHeight();

	v2s32 lower_right = upper_left;
	lower_right.X += m_guitext_profiler->getTextWidth() + 5;
	lower_right.Y += m_guitext_profiler->getTextHeight();

	m_guitext_profiler->setRelativePosition(core::recti(upper_left, lower_right));

	// Really dumb heuristic (we have a fixed number of pages, not a fixed page size)
	const v2u32 window_size = RenderingEngine::getWindowSize();
	if (upper_left.Y + m_guitext_profiler->getTextHeight()
		> window_size.Y * 0.7f) {
		if (m_profiler_max_page < 5) {
			m_profiler_max_page++;
			updateProfiler(); // do it again
		}
	}
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
#if IS_VOPI_ENGINE
		m_chat_bg.setVisible(false);
#endif
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
#if IS_VOPI_ENGINE
		m_status_bg.setVisible(false);
#endif
	}

	if (m_guitext_profiler) {
		m_guitext_profiler->remove();
		m_guitext_profiler = nullptr;
	}
}
