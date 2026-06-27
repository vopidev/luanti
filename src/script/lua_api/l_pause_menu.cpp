// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 grorp

#include "l_pause_menu.h"
#include "client/keycode.h"
#include "gui/mainmenumanager.h"
#include "lua_api/l_internal.h"
#include "client/client.h"
#if IS_VOPI_ENGINE
#include "client/renderingengine.h" // closeDevice() on exit-to-OS (desktop)
#include "gui/modalMenu.h" // GUIModalMenu::quitMenu()
#endif


int ModApiPauseMenu::l_show_touchscreen_layout(lua_State *L)
{
	g_gamecallback->touchscreenLayout();
	return 0;
}


int ModApiPauseMenu::l_is_internal_server(lua_State *L)
{
	lua_pushboolean(L, getClient(L)->m_internal_server);
	return 1;
}


#if IS_VOPI_ENGINE
int ModApiPauseMenu::l_pause_menu_continue(lua_State *L)
{
	// Close the pause menu formspec (the top modal menu) to resume the game.
	if (GUIModalMenu *menu = g_menumgr.tryGetTopMenu())
		menu->quitMenu();
	return 0;
}


int ModApiPauseMenu::l_pause_menu_disconnect(lua_State *L)
{
	g_gamecallback->disconnect();
	return 0;
}


int ModApiPauseMenu::l_pause_menu_change_password(lua_State *L)
{
	g_gamecallback->changePassword();
	return 0;
}


int ModApiPauseMenu::l_pause_menu_exit_to_os(lua_State *L)
{
	g_gamecallback->exitToOS();
#if !defined(__ANDROID__) && !defined(__IOS__)
	RenderingEngine::get_raw_device()->closeDevice();
#endif
	return 0;
}


int ModApiPauseMenu::l_pause_menu_change_volume(lua_State *L)
{
	g_gamecallback->changeVolume();
	return 0;
}
#endif


void ModApiPauseMenu::Initialize(lua_State *L, int top)
{
	API_FCT(show_touchscreen_layout);
	API_FCT(is_internal_server);
#if IS_VOPI_ENGINE
	API_FCT(pause_menu_continue);
	API_FCT(pause_menu_disconnect);
	API_FCT(pause_menu_change_password);
	API_FCT(pause_menu_exit_to_os);
	API_FCT(pause_menu_change_volume);
#endif
}
