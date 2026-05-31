// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 VOPI Team

#include "guiMapElement.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <IVideoDriver.h>
#include "client/client.h"
#include "client/clientenvironment.h"
#include "client/localplayer.h"
#include "client/mapCanvas.h"
#include "client/texturesource.h"
#include "constants.h"
#include "map.h"
#include "mapnode.h"
#include "nodedef.h"
#include "porting.h"
#include "util/numeric.h"

namespace {

// Baked texture resolution (pixels per side). One source pixel maps to
// NODES_PER_PIXEL world nodes, so the map covers MAP_PX * NODES_PER_PIXEL nodes.
// 256 nodes of coverage matches the engine minimap's widest surface mode, so
// each node maps to a smaller on-screen area and the result reads as finely as
// the minimap instead of looking blocky/zoomed-in.
constexpr s32 MAP_PX = 256;
constexpr s32 NODES_PER_PIXEL = 1;
constexpr s32 MAP_EXTENT_NODES = MAP_PX * NODES_PER_PIXEL;

// Rebuild throttle. While the player moves we rebake at most every
// REBUILD_INTERVAL_MS and only after moving REBUILD_MOVE_NODES. But a stationary
// player must still see blocks that stream in after they stopped, so a slower
// REBUILD_STALE_MS fallback forces a rebuild regardless of movement — fresh
// enough to fill in nearby terrain without baking a texture every frame.
constexpr u64 REBUILD_INTERVAL_MS = 500;
constexpr u64 REBUILD_STALE_MS = 2000;
constexpr s16 REBUILD_MOVE_NODES = 4;

// Marker sizes as a fraction of the map width (kept readable on small phone
// screens via the min-pixel floors in drawMarkers).
constexpr f32 POI_DOT_SCALE = 0.02f;     // colour-dot POI marker radius
constexpr f32 POI_ICON_SCALE = 0.035f;   // icon POI marker half-size
constexpr f32 PLAYER_DOT_SCALE = 0.025f; // player centre marker radius

// Background colour for unknown / not-yet-loaded area (fog). Matches Luanti's
// default day horizon sky colour (skyparams.h day_horizon = 144,211,246), so
// the unexplored area reads like looking out into the same sky the player sees
// in-world rather than a flat grey void.
const video::SColor COL_FOG(255, 144, 211, 246);

// Width (in texture pixels) of the soft alpha fade from terrain into the fog at
// the outer data boundary. Interior holes are patched before this runs, so the
// fade only ever happens at the real loaded-area edge.
constexpr s32 FOG_FEATHER = 14;

// Underground rendering. When the player is inside a cave / under a roof, the
// map switches to a "cave view": surface columns that find no top node are
// filled with a soft blue-violet cave tone (instead of the near-black fog),
// and every found node is tinted toward that tone and darkened with depth so
// the area reads as a real underground space rather than a black hole.
const video::SColor COL_CAVE(255, 46, 40, 66);   // empty cave column fill
const video::SColor COL_DEPTH(255, 30, 26, 54);  // tint blended in with depth
// Depth (in nodes below the player) at which the depth tint reaches full
// strength. Beyond this the colour is fully COL_DEPTH-blended + darkened.
constexpr f32 CAVE_DEPTH_FULL = 48.0f;
// How far above the player's head we look for a solid roof to decide whether
// the player counts as "underground".
constexpr s16 ROOF_SCAN = 48;

} // namespace

GUIMapElement::GUIMapElement(gui::IGUIEnvironment *env, gui::IGUIElement *parent,
		s32 id, const core::rect<s32> &rectangle, Client *client) :
	IGUIElement(gui::EGUIET_ELEMENT, env, parent, id, rectangle),
	m_client(client)
{
	m_driver = env->getVideoDriver();
}

GUIMapElement::~GUIMapElement()
{
	if (m_texture)
		m_driver->removeTexture(m_texture);
}

void GUIMapElement::draw()
{
	if (!IsVisible)
		return;

	const core::rect<s32> rect = getAbsoluteClippingRect();

	// Always paint the fog background first, so unexplored / unbaked area reads
	// as sky-coloured mist rather than showing the formspec behind it.
	m_driver->draw2DRectangle(COL_FOG, rect, &rect);

	LocalPlayer *player =
		m_client ? m_client->getEnv().getLocalPlayer() : nullptr;
	if (!player) {
		IGUIElement::draw();
		return;
	}

	const v3s16 center = floatToInt(player->getPosition(), BS);

	// Decide whether to (re)bake the texture: first time, throttled by time,
	// and only when the player has moved enough to matter.
	const u64 now = porting::getTimeMs();
	const s32 moved = std::max({
		std::abs(center.X - m_cached_center.X),
		std::abs(center.Y - m_cached_center.Y),
		std::abs(center.Z - m_cached_center.Z) });
	const u64 since = now - m_last_update_ms;
	if (!m_has_texture ||
			(since >= REBUILD_INTERVAL_MS && moved >= REBUILD_MOVE_NODES) ||
			since >= REBUILD_STALE_MS) {
		rebuildTexture(center);
		m_cached_center = center;
		m_last_update_ms = now;
	}

	if (m_texture) {
		const core::dimension2du tsize = m_texture->getOriginalSize();
		const core::rect<s32> src(0, 0, tsize.Width, tsize.Height);
		m_driver->draw2DImage(m_texture, rect, src, &rect, nullptr, true);
	}

	// Markers are placed relative to the baked center so they stay aligned with
	// the terrain even between rebuilds.
	drawMarkers(rect, m_cached_center);

	IGUIElement::draw();
}

void GUIMapElement::rebuildTexture(v3s16 center)
{
	const NodeDefManager *ndef = m_client->getNodeDefManager();
	Map &map = m_client->getEnv().getMap();
	MapCanvas *canvas = m_client->getMapCanvas();

	video::IImage *img = m_driver->createImage(video::ECF_A8R8G8B8,
		core::dimension2du(MAP_PX, MAP_PX));
	if (!img)
		return;

	// Work on the raw ARGB8888 pixel buffer directly (row-major u32, index
	// py*MAP_PX+px) instead of img->getPixel/setPixel. Those are virtual calls
	// with per-pixel bounds checks; over ~65k px x 4 passes that was the entire
	// ~600ms rebuild cost on A10. Raw pointer access is ~an order of magnitude
	// cheaper. SColor's packed value IS this u32 (A<<24|R<<16|G<<8|B).
	u32 *buf = (u32 *)img->getData();

	// Height per pixel for relief shading (INT16_MIN = no surface found).
	std::vector<s16> heights(MAP_PX * MAP_PX, -32768);

	const s32 half = MAP_EXTENT_NODES / 2;

	// Is the player underground? Look for a solid roof a short way above the
	// head. If found, the map renders in cave view (see COL_CAVE / COL_DEPTH).
	bool underground = false;
	for (s16 dy = 2; dy <= ROOF_SCAN; dy++) {
		bool valid = false;
		MapNode rn = map.getNode(v3s16(center.X, center.Y + dy, center.Z), &valid);
		if (!valid)
			continue;
		content_t rc = rn.getContent();
		if (rc == CONTENT_IGNORE || rc == CONTENT_AIR)
			continue;
		if (ndef->get(rc).drawtype != NDT_AIRLIKE) {
			underground = true;
			break;
		}
	}

	// First pass: assemble the player-centered window from the persistent
	// fog-of-war canvas (filled continuously by Client::step harvest). A cell
	// the canvas has never recorded stays fog. Cache the last tile across the
	// inner loop — the window spans only ~9 tiles, so consecutive pixels
	// overwhelmingly hit the same tile.
	//
	// IMPORTANT: cache on the tile COORD, not on (cur_tile != null). A large
	// part of the render window is usually unexplored (findTileReadonly returns
	// null there); keying the cache on a null check re-queried the canvas for
	// every single fog pixel — tens of thousands of make_unique<MapTile>(96KiB)
	// + disk-stat calls per rebuild. That was the ~600ms freeze. Tracking
	// "which coord we last looked up" makes a fog tile cost one lookup, not one
	// per pixel.
	const MapTile *cur_tile = nullptr;
	s16 cur_tx = -32768, cur_tz = -32768;
	bool have_lookup = false;

	for (s32 py = 0; py < MAP_PX; py++)
	for (s32 px = 0; px < MAP_PX; px++) {
		// North (+Z) is up on screen, East (+X) is right.
		const s16 wx = center.X + (s16)(px * NODES_PER_PIXEL - half);
		const s16 wz = center.Z + (s16)(half - py * NODES_PER_PIXEL);

		video::SColor out(0, 0, 0, 0); // transparent => fog shows through
		bool found = false;

		if (canvas) {
			const s16 tx = MapCanvas::tileCoord(wx);
			const s16 tz = MapCanvas::tileCoord(wz);
			if (!have_lookup || tx != cur_tx || tz != cur_tz) {
				cur_tile = canvas->findTileReadonly(tx, tz);
				cur_tx = tx;
				cur_tz = tz;
				have_lookup = true;
			}
			if (cur_tile) {
				const s32 ox = wx & (MAPCANVAS_TILE_NODES - 1);
				const s32 oz = wz & (MAPCANVAS_TILE_NODES - 1);
				const s32 ci = oz * MAPCANVAS_TILE_NODES + ox;
				const u32 argb = cur_tile->colour[ci];
				if ((argb >> 24) != 0) { // alpha != 0 => explored
					video::SColor tilecolor(argb);
					const s16 wy = cur_tile->height[ci];

					// Cave view is applied at RENDER time (never stored), using
					// the player's CURRENT Y, so an area explored from below
					// isn't permanently dark on the surface map.
					if (underground && wy < center.Y) {
						const f32 t = core::clamp(
							(f32)(center.Y - wy) / CAVE_DEPTH_FULL, 0.0f, 1.0f);
						const f32 keep = 1.0f - 0.6f * t;
						tilecolor.setRed((s32)(tilecolor.getRed() * keep +
							COL_DEPTH.getRed() * (1.0f - keep)));
						tilecolor.setGreen((s32)(tilecolor.getGreen() * keep +
							COL_DEPTH.getGreen() * (1.0f - keep)));
						tilecolor.setBlue((s32)(tilecolor.getBlue() * keep +
							COL_DEPTH.getBlue() * (1.0f - keep)));
					}
					tilecolor.setAlpha(255);
					out = tilecolor;
					heights[py * MAP_PX + px] = wy;
					found = true;
				}
			}
		}

		// In cave view, empty columns (no surface) get a soft cave fill instead
		// of falling through to the near-black fog.
		if (!found && underground)
			out = COL_CAVE;

		buf[py * MAP_PX + px] = out.color;
	}

	// Second pass: relief shading. Use the *signed height difference* to the
	// northern neighbour as a smooth, proportional slope factor rather than two
	// discrete brighten/darken steps. The discrete version produced visible
	// horizontal banding on gentle slopes; a proportional factor reads as a
	// continuous hillshade, much closer to the engine minimap's look.
	//
	// The shading is ASYMMETRIC on purpose: upward slopes get a gentle highlight
	// (keep the surface from looking blown-out), but DOWNWARD drops are darkened
	// much harder and deeper. Cave mouths / terrain holes are exactly these
	// sharp drops, so this makes them read as distinct dark pits standing out
	// against the flat terrain — which is the goal here.
	for (s32 py = 1; py < MAP_PX; py++)
	for (s32 px = 0; px < MAP_PX; px++) {
		const s16 h = heights[py * MAP_PX + px];
		const s16 hn = heights[(py - 1) * MAP_PX + px];
		if (h == -32768 || hn == -32768)
			continue;

		const s16 d = h - hn; // >0 rises to the north, <0 drops away
		f32 factor;
		if (d >= 0)
			factor = 1.0f + core::clamp((f32)d * 0.05f, 0.0f, 0.30f);
		else
			// Steeper slope and a much deeper floor for drops (pits/cave mouths).
			factor = 1.0f + core::clamp((f32)d * 0.09f, -0.62f, 0.0f);

		if (factor > 0.999f && factor < 1.001f)
			continue;

		u32 &c = buf[py * MAP_PX + px];
		const u32 a = c & 0xFF000000u;
		const s32 r = core::clamp((s32)(((c >> 16) & 0xFF) * factor), 0, 255);
		const s32 g = core::clamp((s32)(((c >> 8) & 0xFF) * factor), 0, 255);
		const s32 b = core::clamp((s32)((c & 0xFF) * factor), 0, 255);
		c = a | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
	}

	// Third pass: fill small interior holes. A column can come back empty even
	// inside loaded terrain (a single un-meshed block, a scan gap). Left alone
	// these read as dark specks/squares in the middle of the map. If an empty
	// pixel is surrounded by mostly-real neighbours it is an interior hole, so
	// we copy a neighbour's colour/height into it. Done into scratch copies so
	// the decision uses only original data (no cascading fills in one pass).
	//
	// Surface view only: cave view fills empties with an opaque cave tone on
	// purpose, so there are no holes to patch there.
	if (!underground) {
		std::vector<s16> filled_h = heights;
		for (s32 py = 0; py < MAP_PX; py++)
		for (s32 px = 0; px < MAP_PX; px++) {
			const s32 idx = py * MAP_PX + px;
			if (heights[idx] != -32768)
				continue;

			// Count real 4-neighbours and remember one to copy from.
			s32 real = 0, src_idx = -1;
			if (px > 0 && heights[idx - 1] != -32768) { real++; src_idx = idx - 1; }
			if (px < MAP_PX - 1 && heights[idx + 1] != -32768) { real++; src_idx = idx + 1; }
			if (py > 0 && heights[idx - MAP_PX] != -32768) { real++; src_idx = idx - MAP_PX; }
			if (py < MAP_PX - 1 && heights[idx + MAP_PX] != -32768) { real++; src_idx = idx + MAP_PX; }

			// 3+ real neighbours => surrounded => interior hole, patch it.
			if (real >= 3 && src_idx >= 0) {
				buf[idx] = buf[src_idx];
				filled_h[idx] = heights[src_idx];
			}
		}
		heights.swap(filled_h);
	}

	// Fourth pass: alpha-feather the OUTER data edge into the fog. Interior
	// holes were just patched, so the only empty pixels left form the genuine
	// loaded-area boundary. A cheap two-sweep Chebyshev distance transform gives
	// each terrain pixel its distance to the nearest empty pixel; we ramp alpha
	// from 0 at the very edge up to opaque FOG_FEATHER pixels inside, so the map
	// dissolves smoothly into the sky-coloured fog instead of ending on a hard
	// block-shaped step. Surface view only (cave view has no real fog edge).
	if (!underground) {
		const s32 N = MAP_PX * MAP_PX;
		const s32 INF = MAP_PX * 4;
		std::vector<s32> dist(N, INF);
		for (s32 i = 0; i < N; i++)
			if (heights[i] == -32768)
				dist[i] = 0; // empty pixels are the edge (distance 0)

		// Forward sweep: propagate from top/left.
		for (s32 py = 0; py < MAP_PX; py++)
		for (s32 px = 0; px < MAP_PX; px++) {
			const s32 idx = py * MAP_PX + px;
			s32 d = dist[idx];
			if (px > 0) d = std::min(d, dist[idx - 1] + 1);
			if (py > 0) d = std::min(d, dist[idx - MAP_PX] + 1);
			dist[idx] = d;
		}
		// Backward sweep: propagate from bottom/right, then apply alpha ramp.
		for (s32 py = MAP_PX - 1; py >= 0; py--)
		for (s32 px = MAP_PX - 1; px >= 0; px--) {
			const s32 idx = py * MAP_PX + px;
			s32 d = dist[idx];
			if (px < MAP_PX - 1) d = std::min(d, dist[idx + 1] + 1);
			if (py < MAP_PX - 1) d = std::min(d, dist[idx + MAP_PX] + 1);
			dist[idx] = d;

			if (heights[idx] != -32768 && d < FOG_FEATHER) {
				const u32 alpha = (u32)(255 * d / FOG_FEATHER);
				buf[idx] = (buf[idx] & 0x00FFFFFFu) | (alpha << 24);
			}
		}
	}

	if (m_texture)
		m_driver->removeTexture(m_texture);

	// Unique texture name per element instance to avoid cache collisions when
	// several map[] elements exist at once.
	const std::string name = "__kc_map_" + std::to_string((u64)(intptr_t)this);
	m_texture = m_driver->addTexture(name.c_str(), img);
	img->drop();
	m_has_texture = (m_texture != nullptr);
}

void GUIMapElement::drawMarkers(const core::rect<s32> &rect, v3s16 center)
{
	const f32 w = (f32)rect.getWidth();
	const f32 h = (f32)rect.getHeight();
	const v2s32 origin = rect.UpperLeftCorner;
	const f32 extent = (f32)MAP_EXTENT_NODES;

	// Custom points (markers passed from Lua). Icon size is a bit larger than
	// the plain colour square so kawaii icons read clearly on a phone screen.
	const s32 r = std::max<s32>(2, (s32)(POI_DOT_SCALE * w));
	const s32 ir = std::max<s32>(6, (s32)(POI_ICON_SCALE * w));
	ITextureSource *tsrc = m_client ? m_client->tsrc() : nullptr;
	for (const MapPoint &p : m_points) {
		const f32 fx = ((f32)p.world_pos.X - (f32)center.X) / extent + 0.5f;
		const f32 fz = 0.5f - ((f32)p.world_pos.Z - (f32)center.Z) / extent;
		if (fx < 0.0f || fx > 1.0f || fz < 0.0f || fz > 1.0f)
			continue;

		const s32 cx = origin.X + (s32)(fx * w);
		const s32 cy = origin.Y + (s32)(fz * h);

		// Prefer an icon texture if the point carries one and it loads.
		video::ITexture *icon = nullptr;
		if (!p.icon.empty() && tsrc)
			icon = tsrc->getTexture(p.icon);

		if (icon) {
			const core::dimension2du isz = icon->getOriginalSize();
			const core::rect<s32> src(0, 0, isz.Width, isz.Height);
			const core::rect<s32> dest(cx - ir, cy - ir, cx + ir, cy + ir);
			m_driver->draw2DImage(icon, dest, src, &rect, nullptr, true);
		} else {
			const core::rect<s32> dest(cx - r, cy - r, cx + r, cy + r);
			m_driver->draw2DRectangle(p.color, dest, &rect);
		}
	}

	// Player marker — always at the center (the map is centered on the player).
	const s32 cx = origin.X + (s32)(0.5f * w);
	const s32 cy = origin.Y + (s32)(0.5f * h);
	const s32 pr = std::max<s32>(3, (s32)(PLAYER_DOT_SCALE * w));
	const core::rect<s32> outer(cx - pr, cy - pr, cx + pr, cy + pr);
	const core::rect<s32> inner(cx - pr + 1, cy - pr + 1, cx + pr - 1, cy + pr - 1);
	m_driver->draw2DRectangle(video::SColor(255, 0, 0, 0), outer, &rect);
	m_driver->draw2DRectangle(video::SColor(255, 255, 255, 255), inner, &rect);
}

bool GUIMapElement::OnEvent(const SEvent &event)
{
	// Phase 1: no interaction yet; let events pass through.
	return IGUIElement::OnEvent(event);
}
