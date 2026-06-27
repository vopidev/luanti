// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 grorp

#pragma once

#include "l_base.h"

class ModApiPauseMenu: public ModApiBase
{
private:
	static int l_show_touchscreen_layout(lua_State *L);
	static int l_is_internal_server(lua_State *L);

#if IS_VOPI_ENGINE
	// VOPI: pause menu button actions, called from the Lua pause menu UI
	// (builtin/pause_menu/). They forward to g_gamecallback, mirroring the old
	// hardcoded pause menu handler.
	static int l_pause_menu_continue(lua_State *L);
	static int l_pause_menu_disconnect(lua_State *L);
	static int l_pause_menu_change_password(lua_State *L);
	static int l_pause_menu_exit_to_os(lua_State *L);
	static int l_pause_menu_change_volume(lua_State *L);
#endif

public:
	static void Initialize(lua_State *L, int top);
};
