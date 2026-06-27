// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 grorp

#include "s_pause_menu.h"
#include "cpp_api/s_internal.h"
#if IS_VOPI_ENGINE
#include "common/helper.h" // readParam<std::string>
#endif

void ScriptApiPauseMenu::open_settings()
{
	SCRIPTAPI_PRECHECKHEADER

	int error_handler = PUSH_ERROR_HANDLER(L);

	lua_getglobal(L, "core");
	lua_getfield(L, -1, "open_settings");

	PCALL_RES(lua_pcall(L, 0, 0, error_handler));

	lua_pop(L, 2); // Pop core, error handler
}

#if IS_VOPI_ENGINE
std::string ScriptApiPauseMenu::get_pause_menu_formspec(bool simple_singleplayer_mode)
{
	SCRIPTAPI_PRECHECKHEADER

	int error_handler = PUSH_ERROR_HANDLER(L);

	lua_getglobal(L, "core");
	lua_getfield(L, -1, "get_pause_menu_formspec");
	lua_pushboolean(L, simple_singleplayer_mode);

	PCALL_RES(lua_pcall(L, 1, 1, error_handler));

	std::string formspec = lua_isstring(L, -1) ? readParam<std::string>(L, -1) : "";
	lua_pop(L, 3); // Pop formspec, core, error handler
	return formspec;
}
#endif
