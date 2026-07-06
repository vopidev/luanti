-- Default in-game pause menu (the ESC menu).
--
-- The engine fetches this formspec via core.get_pause_menu_formspec() and
-- routes button input back through core.register_on_formspec_input() under
-- the form name "MT_PAUSE_MENU" (see GameFormSpec::showPauseMenu). A game
-- distribution can replace this file to restyle the menu; the engine only
-- requires get_pause_menu_formspec() to return a formspec string (return ""
-- to show no menu).

local PAUSE_FORMNAME = "MT_PAUSE_MENU"

function core.get_pause_menu_formspec(simple_singleplayer_mode)
	local ypos = simple_singleplayer_mode and 0.7 or 0.1
	local function row(elem, name, label)
		local s = ("%s[4,%.1f;3,0.5;%s;%s]"):format(elem, ypos, name, label)
		ypos = ypos + 1
		return s
	end

	local fs = {
		"formspec_version[1]size[11,5.5,true]",
		row("button_exit", "btn_continue", fgettext("Continue")),
	}
	if simple_singleplayer_mode then
		fs[#fs + 1] = ("field[4.95,0;5,1.5;;%s;]"):format(fgettext("Game paused"))
	else
		fs[#fs + 1] = row("button", "btn_change_password",
				fgettext("Change Password"))
	end
	fs[#fs + 1] = row("button", "btn_settings", fgettext("Settings"))
	fs[#fs + 1] = row("button_exit", "btn_exit_menu", fgettext("Exit to Menu"))
	fs[#fs + 1] = row("button_exit", "btn_exit_os", fgettext("Exit to OS"))

	return table.concat(fs)
end

core.register_on_formspec_input(function(formname, fields)
	if formname ~= PAUSE_FORMNAME then
		return
	end
	if fields.btn_settings then
		core.open_settings()
	elseif fields.btn_change_password then
		core.pause_menu_change_password()
	elseif fields.btn_exit_menu then
		core.pause_menu_disconnect()
	elseif fields.btn_exit_os then
		core.pause_menu_exit_to_os()
	end
	-- btn_continue is a button_exit: it closes the menu and resumes the game
	-- on its own, so no explicit handler is needed here.
end)
