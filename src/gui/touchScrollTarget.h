// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 VOPI Team

#pragma once

#include "irrlichttypes_bloated.h"

#if IS_VOPI_ENGINE

// VOPI Engine: a GUI element whose content can be panned by a touch drag and
// glided by inertial scrolling (a "fling"). The formspec touch gesture layer
// (GUIFormSpecMenu::handleTouchScroll) speaks to its targets — scroll
// containers, read-only textareas — only through this interface.
class ITouchScrollTarget
{
public:
	virtual ~ITouchScrollTarget() = default;

	// True if there is currently a non-empty scroll range.
	virtual bool isScrollable() = 0;

	// Current scroll position, in the target's own scroll units.
	virtual s32 getScrollPos() const = 0;

	// Projects a pointer delta onto the target's scroll axis.
	virtual s32 axisDelta(const v2s32 &delta) const = 0;

	// Projects a pointer delta onto the axis perpendicular to scrolling.
	virtual s32 crossAxisDelta(const v2s32 &delta) const = 0;

	// Pans the content by a pixel delta along the scroll axis, relative to the
	// given reference scroll position (finger-follow). Clamps to the range.
	virtual void scrollByPixels(s32 origin_scrollpos, const v2s32 &pixel_delta) = 0;

	// Starts inertial scrolling with the given finger velocity in pixels per
	// millisecond along the scroll axis. A small velocity is ignored.
	virtual void startFling(f32 axis_velocity_px_per_ms) = 0;
	// Stops any in-flight inertial scrolling.
	virtual void stopFling() = 0;
	virtual bool isFlinging() const = 0;
};

// Inertial-scroll tuning shared by all fling implementations, so every
// scrollable surface glides with the same feel; tweak here, device-tested.
namespace touch_scroll
{
	// Fraction of velocity retained per 16.667 ms frame (~0.7 s glide).
	constexpr f32 FLING_DECAY_PER_FRAME = 0.95f;
	// Minimum launch speed needed to start a fling (finger pixels per ms).
	constexpr f32 FLING_MIN_START_SPEED = 0.2f;
	// Speed below which an in-flight fling stops (finger pixels per ms).
	constexpr f32 FLING_MIN_SPEED = 0.05f;
	// Cap on launch speed so a hard flick can't rocket the content (px per ms).
	constexpr f32 FLING_MAX_SPEED = 6.0f;
	// Clamp on integration steps after a frame hitch so content can't teleport.
	constexpr u64 FLING_MAX_STEP_MS = 64;
}

#endif // IS_VOPI_ENGINE
