// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#pragma once

#include "irrlichttypes_bloated.h"
#include "inventory.h"
#include "mapnode.h" // MapNode (VOPI: node-selection snapshot)
#include "util/basic_macros.h"
#include <string>
#include <string_view>

#define PLAYERNAME_SIZE 20

#define PLAYERNAME_ALLOWED_CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"
#define PLAYERNAME_ALLOWED_CHARS_USER_EXPL "'a' to 'z', 'A' to 'Z', '0' to '9', '-', '_'"

bool is_valid_player_name(std::string_view name);

struct PlayerFovSpec
{
	f32 fov;

	// Whether to multiply the client's FOV or to override it
	bool is_multiplier;

	// The time to be take to trasition to the new FOV value.
	// Transition is instantaneous if omitted. Omitted by default.
	f32 transition_time = 0;

	inline bool operator==(const PlayerFovSpec &other) const {
		// transition_time is compared here since that could be relevant
		// when aborting a running transition.
		return fov == other.fov && is_multiplier == other.is_multiplier &&
			transition_time == other.transition_time;
	}
	inline bool operator!=(const PlayerFovSpec &other) const {
		return !(*this == other);
	}
};

#if IS_VOPI_ENGINE
struct PlayerViewBobbingSpec
{
	f32 amount;

	// Whether to multiply the client's view bobbing amount or to override it
	bool is_multiplier;

	// The time to be take to transition to the new view bobbing value.
	// Transition is instantaneous if omitted. Omitted by default.
	f32 transition_time = 0;

	inline bool operator==(const PlayerViewBobbingSpec &other) const {
		return amount == other.amount && is_multiplier == other.is_multiplier &&
			transition_time == other.transition_time;
	}
	inline bool operator!=(const PlayerViewBobbingSpec &other) const {
		return !(*this == other);
	}
};
#endif

struct PlayerControl
{
	PlayerControl() = default;

	PlayerControl(
		bool a_up, bool a_down, bool a_left, bool a_right,
		bool a_jump, bool a_aux1, bool a_sneak,
		bool a_zoom,
		bool a_dig, bool a_place,
		float a_pitch, float a_yaw,
		float a_movement_speed, float a_movement_direction
	)
	{
		// Encode direction keys into a single value so nobody uses it accidentally
		// as movement_{speed,direction} is supposed to be the source of truth.
		direction_keys = (a_up&1) | ((a_down&1) << 1) |
			((a_left&1) << 2) | ((a_right&1) << 3);
		jump = a_jump;
		aux1 = a_aux1;
		sneak = a_sneak;
		zoom = a_zoom;
		dig = a_dig;
		place = a_place;
		pitch = a_pitch;
		yaw = a_yaw;
		movement_speed = a_movement_speed;
		movement_direction = a_movement_direction;
	}

	// Sets movement_speed and movement_direction according to direction_keys
	// if direction_keys != 0, otherwise leaves them unchanged to preserve
	// joystick input.
	void setMovementFromKeys();

	// For client use
	u32 getKeysPressed() const;
	inline bool isMoving() const { return movement_speed > 0.001f; }

	// For server use
	void unpackKeysPressed(u32 keypress_bits);
	v2f getMovement() const;

	u8 direction_keys = 0;
	bool jump = false;
	bool aux1 = false;
	bool sneak = false;
	bool zoom = false;
	bool dig = false;
	bool place = false;
	// Note: These two are NOT available on the server
	float pitch = 0.0f;
	float yaw = 0.0f;
	float movement_speed = 0.0f;
	float movement_direction = 0.0f;
};

struct PlayerPhysicsOverride
{
	float speed = 1.f;
	float jump = 1.f;
	float gravity = 1.f;

	bool sneak = true;
	bool sneak_glitch = false;
	// "Temporary" option for old move code
	bool new_move = true;

	float speed_climb = 1.f;
	float speed_crouch = 1.f;
	float liquid_fluidity = 1.f;
	float liquid_fluidity_smooth = 1.f;
	float liquid_sink = 1.f;
	float acceleration_default = 1.f;
	float acceleration_air = 1.f;
	float speed_fast = 1.f;
	float acceleration_fast = 1.f;
	float speed_walk = 1.f;

#if IS_VOPI_ENGINE
	// When true, disables engine-side swim up (jump key in liquid)
	// Allows Lua to implement custom swimming behavior
	bool disable_swim_up = false;
	// When true, disables engine-side swim down (sneak key in liquid)
	// Allows Lua to implement custom swimming behavior
	bool disable_swim_down = false;
#endif

	bool operator==(const PlayerPhysicsOverride &other) const;
	bool operator!=(const PlayerPhysicsOverride &other) const {
		return !(*this == other);
	}
};

/// @note numeric values are part of network protocol
enum CameraMode : int {
	// not a mode. indicates that any may be used.
	CAMERA_MODE_ANY = 0,
	CAMERA_MODE_FIRST,
	CAMERA_MODE_THIRD,
	CAMERA_MODE_THIRD_FRONT,

	CameraMode_END // Dummy for validity check
};

extern const struct EnumString es_CameraMode[];

struct HudElement;

class Player
{
public:

	Player(const std::string &name, IItemDefManager *idef);
	virtual ~Player() = 0;

	DISABLE_CLASS_COPY(Player);

	// in BS-space
	inline void setSpeed(v3f speed)
	{
		m_speed = speed;
	}

	// in BS-space
	v3f getSpeed() const { return m_speed; }

	const std::string& getName() const { return m_name; }

	u32 getFreeHudID()
	{
		size_t size = hud.size();
		for (size_t i = 0; i != size; i++) {
			if (!hud[i])
				return i;
		}
		return size;
	}

	CameraMode allowed_camera_mode = CAMERA_MODE_ANY;

	v3f eye_offset_first;
	v3f eye_offset_third;
	v3f eye_offset_third_front;

	Inventory inventory;

	f32 movement_acceleration_default;
	f32 movement_acceleration_air;
	f32 movement_acceleration_fast;
	f32 movement_speed_walk;
	f32 movement_speed_crouch;
	f32 movement_speed_fast;
	f32 movement_speed_climb;
	f32 movement_speed_jump;
	f32 movement_liquid_fluidity;
	f32 movement_liquid_fluidity_smooth;
	f32 movement_liquid_sink;
	f32 movement_gravity;

	v2f local_animations[4];
	float local_animation_speed;

	std::string inventory_formspec;
	std::string formspec_prepend;

	PlayerControl control;
	const PlayerControl& getPlayerControl() { return control; }

	PlayerPhysicsOverride physics_override;

	// Returns non-empty `selected` ItemStack. `hand` is a fallback, if specified
	ItemStack &getWieldedItem(ItemStack *selected, ItemStack *hand) const;
	void setWieldIndex(u16 index);
	u16 getWieldIndex();

	bool setFov(const PlayerFovSpec &spec)
	{
		if (m_fov_override_spec == spec)
			return false;
		m_fov_override_spec = spec;
		return true;
	}

	const PlayerFovSpec &getFov() const
	{
		return m_fov_override_spec;
	}

#if IS_VOPI_ENGINE
	bool setViewBobbing(const PlayerViewBobbingSpec &spec)
	{
		if (m_view_bobbing_override_spec == spec)
			return false;
		m_view_bobbing_override_spec = spec;
		return true;
	}

	const PlayerViewBobbingSpec &getViewBobbing() const
	{
		return m_view_bobbing_override_spec;
	}
#endif

	const auto &getHudElements() const { return hud; }
	HudElement* getHud(u32 id);
	u32         addHud(HudElement* hud);
	HudElement* removeHud(u32 id);
	void        clearHud();

	u32 hud_flags;
	s32 hud_hotbar_itemcount;

#if IS_VOPI_ENGINE
	// VOPI: bitmask of hidden on-screen touch buttons.
	// Bit index = touch_gui_button_id (see gui/touchscreenlayout.h);
	// bit set = that button is hidden. Default 0 = all visible.
	u32 touch_hidden_mask = 0;

	// VOPI: when true, the player cannot dig/place/punch/use the world.
	// Enforced client-side (TouchControls::applyContextControls is skipped) and
	// server-side (handleCommand_Interact rejects the action). Default false.
	bool block_interaction = false;

	// VOPI: clamp range (in degrees) applied to the camera pitch each frame.
	// Set per-player from Lua (set_camera_pitch_range) and synced server->client.
	// Defaults to the full range; -90 = straight up, 90 = straight down.
	f32 camera_pitch_min = -90.0f;
	f32 camera_pitch_max = 90.0f;

	// VOPI: optional clamp arc (in degrees) for the camera yaw, set per-player
	// from Lua (set_camera_yaw_range) and synced server->client. Yaw is cyclic,
	// so the clamp is applied around the arc centre; only active when
	// camera_yaw_limited is true (and only in first person, client-side).
	bool camera_yaw_limited = false;
	f32 camera_yaw_min = 0.0f;
	f32 camera_yaw_max = 0.0f;

	// VOPI: the node the player currently has selected (pointed at), as last
	// reported by the client via TOSERVER_NODE_SELECTED. Used to fire the
	// on_selectnode / on_deselectnode callbacks. m_has_selected_node == false
	// means no node is currently selected.
	bool m_has_selected_node = false;
	v3s16 m_selected_node;
	// Snapshot of the node content at select time, so on_deselectnode reports
	// the node that was selected even if it was dug or replaced since.
	MapNode m_selected_node_content;

	// VOPI: token bucket rate-limiting TOSERVER_NODE_SELECTED handling, so a
	// modified client alternating positions (which defeats the same-position
	// dedupe) cannot force node callbacks at network rate. Refilled on demand
	// from the wall clock; see Server::handleCommand_NodeSelected.
	f32 m_node_select_tokens = 0.0f;
	u64 m_node_select_last_ms = 0;
#endif

	// Get actual usable number of hotbar items (clamped to size of "main" list)
	u16 getMaxHotbarItemcount();

protected:
	std::string m_name;
	v3f m_speed; // velocity; in BS-space
	u16 m_wield_index = 0;
	PlayerFovSpec m_fov_override_spec = { 0.0f, false, 0.0f };
#if IS_VOPI_ENGINE
	PlayerViewBobbingSpec m_view_bobbing_override_spec = { 0.0f, false, 0.0f };
#endif

private:
	std::vector<HudElement *> hud;
};
