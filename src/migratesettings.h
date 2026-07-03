// Minetest
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "config.h"
#include "settings.h"
#include "server.h"

void migrate_settings()
{
	// Converts opaque_water to translucent_liquids
	if (g_settings->existsLocal("opaque_water")) {
		g_settings->set("translucent_liquids",
				g_settings->getBool("opaque_water") ? "false" : "true");
		g_settings->remove("opaque_water");
	}

	// Converts enable_touch to touch_controls/touch_gui
	if (g_settings->existsLocal("enable_touch")) {
		bool value = g_settings->getBool("enable_touch");
		g_settings->setBool("touch_controls", value);
		g_settings->setBool("touch_gui", value);
		g_settings->remove("enable_touch");
	}

	// Disables anticheat
	if (g_settings->existsLocal("disable_anticheat")) {
		if (g_settings->getBool("disable_anticheat")) {
			g_settings->setFlagStr("anticheat_flags", 0, flagdesc_anticheat);
		}
		g_settings->remove("disable_anticheat");
	}

	// Convert touch_use_crosshair to touch_interaction_style
	if (g_settings->existsLocal("touch_use_crosshair")) {
		bool value = g_settings->getBool("touch_use_crosshair");
		g_settings->set("touch_interaction_style", value ? "tap_crosshair" : "tap");
		g_settings->remove("touch_use_crosshair");
	}

	// turn FXAA into its own setting
	if (g_settings->get("antialiasing") == "fxaa") {
		g_settings->setBool("fxaa", true);
		g_settings->remove("antialiasing");
	}

#if IS_VOPI_ENGINE && (defined(__ANDROID__) || defined(__IOS__))
	// Repair configs damaged by the old mobile memory-pressure response,
	// which ratcheted viewing_range and client_mapblock_limit down in the
	// user layer and let the result persist. Nothing else ever writes
	// client_mapblock_limit to the user layer, so its presence identifies
	// the damage; drop both keys so the per-device defaults apply again.
	if (g_settings->existsLocal("client_mapblock_limit")) {
		g_settings->remove("client_mapblock_limit");
		g_settings->remove("viewing_range");
	}
#endif
}
