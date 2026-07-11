// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 VOPI Team

#pragma once

#include "irrlichttypes_bloated.h"

#if IS_VOPI_ENGINE

#include <algorithm>
#include <cmath>

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
	// Reference frame length the per-frame decay constant is expressed in (ms).
	constexpr f32 FLING_FRAME_MS = 16.667f;

	// Shared inertial-scroll integrator: a float-pixel position driven by a
	// decaying velocity, so every scrollable surface glides with the same
	// physics. Owners seed it with start(), advance it once per frame with
	// step(), and write the integer position back to their own scroll
	// mechanism. Velocity follows the POSITION axis: positive velocity
	// increases px — callers negate it when their finger axis runs against
	// their scroll-position axis.
	struct FlingState
	{
		bool active = false;
		f32 vel = 0.0f;  //< axis velocity, pixels per millisecond
		f32 px = 0.0f;   //< integrated position, pixels
		u64 last_ms = 0;

		// Arms the fling (re-seeding any glide in flight). Returns false and
		// leaves it inactive when |velocity| is below the launch gate; the
		// velocity is capped to FLING_MAX_SPEED.
		bool start(f32 velocity_px_per_ms, f32 start_px, u64 now_ms)
		{
			if (std::fabs(velocity_px_per_ms) < FLING_MIN_START_SPEED) {
				active = false;
				return false;
			}
			vel = std::max(-FLING_MAX_SPEED,
					std::min(velocity_px_per_ms, FLING_MAX_SPEED));
			px = start_px;
			last_ms = now_ms;
			active = true;
			return true;
		}

		void stop()
		{
			active = false;
			vel = 0.0f;
		}

		// Integrates up to now_ms, clamping px to [lo, hi]. Returns true while
		// the glide continues; deactivates and returns false on a bound hit or
		// once friction decays the velocity below the stop gate. px holds the
		// final clamped position either way — apply it after every call.
		bool step(u64 now_ms, f32 lo, f32 hi)
		{
			if (!active)
				return false;
			u64 dt = now_ms - last_ms;
			last_ms = now_ms;
			if (dt == 0)
				return true;
			if (dt > FLING_MAX_STEP_MS)
				dt = FLING_MAX_STEP_MS; // clamp after a hitch: no teleporting

			px += vel * (f32)dt;

			bool hit_bound = false;
			if (px <= lo) {
				px = lo;
				hit_bound = true;
			} else if (px >= hi) {
				px = hi;
				hit_bound = true;
			}

			// Exponential friction, frame-rate independent.
			vel *= std::pow(FLING_DECAY_PER_FRAME, (f32)dt / FLING_FRAME_MS);
			if (hit_bound || std::fabs(vel) < FLING_MIN_SPEED) {
				stop();
				return false;
			}
			return true;
		}
	};
}

#endif // IS_VOPI_ENGINE
