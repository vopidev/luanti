// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 grorp

#pragma once

#include "cpp_api/s_base.h"
#include <string>

class ScriptApiPauseMenu : virtual public ScriptApiBase
{
public:
	void open_settings();

#if IS_VOPI_ENGINE
	// VOPI: fetch the pause menu formspec from the Lua pause env, by calling the
	// global core.get_pause_menu_formspec(simple_singleplayer_mode). Lets the
	// pause menu UI live in builtin/pause_menu/ instead of being built in C++.
	std::string get_pause_menu_formspec(bool simple_singleplayer_mode);
#endif
};
