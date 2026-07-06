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
#include "porting.h"
#include "util/numeric.h"

namespace {

// Baked texture resolution (pixels per side). The map window spans
// m_view_nodes world nodes across this width (the zoom level), so one texture
// pixel maps to (m_view_nodes / MAP_PX) world nodes — which may be < 1 (zoomed
// in, a node covers several pixels via nearest upscale) or > 1 (zoomed out).
constexpr s32 MAP_PX = 256;

// Zoom range, in world nodes spanned across the window. 128 = closest (a node
// is a crisp 2x2 px block), 1024 = widest. The texture stays MAP_PX so the
// rebuild cost is constant regardless of zoom.
constexpr s32 VIEW_NODES_MIN = 128;
constexpr s32 VIEW_NODES_MAX = 1024;

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

// Width (in texture pixels) of the soft colour fade from terrain into the fog
// at the outer data boundary. Interior holes are patched before this runs, so
// the fade only ever happens at the real loaded-area edge. The map always
// renders as a surface map with this fixed fog — there is no separate cave/
// underground view, so the unexplored colour never changes with where the
// player is or the time of day.
constexpr s32 FOG_FEATHER = 14;

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

	// Release the canvas keep-window we announced from rebuildTexture, so the
	// harvest's eviction stops protecting our (now dead) render window.
	if (m_client) {
		if (MapCanvas *canvas = m_client->getMapCanvas())
			canvas->clearRenderWindowHint();
	}
}

void GUIMapElement::setViewNodes(s32 nodes)
{
	m_view_nodes = core::clamp(nodes, VIEW_NODES_MIN, VIEW_NODES_MAX);
}

void GUIMapElement::draw()
{
	if (!IsVisible)
		return;

	const core::rect<s32> rect = getAbsoluteClippingRect();

	// Always paint the fog background first, so unexplored / unbaked area reads
	// as a fixed sky-coloured mist rather than showing the scene behind it.
	m_driver->draw2DRectangle(COL_FOG, rect, &rect);

	LocalPlayer *player =
		m_client ? m_client->getEnv().getLocalPlayer() : nullptr;
	if (!player) {
		IGUIElement::draw();
		return;
	}

	// The map is centered on the pinned focus if one is set (e.g. a POI the
	// player selected), otherwise it follows the local player.
	//
	// Unit care: m_focus is already in WORLD NODE coords (the formspec passes
	// wx,wy,wz — the same units as the markers' world_pos, which drawMarkers
	// subtracts straight from `center`). So it must only be rounded to s16,
	// NOT divided by BS. Using floatToInt(.., 1.0f) rounds without scaling.
	// The player branch DOES divide by BS because getPosition() is BS-scaled.
	// (Previously this used floatToInt(m_focus, BS), which divided the node
	// coords by BS=10 and dropped the center near the origin — almost always
	// unexplored, so focusing on any POI showed nothing but fog.)
	const v3s16 center = m_has_focus
		? floatToInt(m_focus, 1.0f)
		: floatToInt(player->getPosition(), BS);

	// Decide whether to (re)bake the texture: first time, on zoom change, when
	// throttle elapsed and the center moved enough, or after a stale interval.
	const u64 now = porting::getTimeMs();
	const s32 moved = std::max({
		std::abs(center.X - m_cached_center.X),
		std::abs(center.Y - m_cached_center.Y),
		std::abs(center.Z - m_cached_center.Z) });
	const u64 since = now - m_last_update_ms;
	if (!m_has_texture ||
			m_view_nodes != m_cached_view_nodes ||
			(since >= REBUILD_INTERVAL_MS && moved >= REBUILD_MOVE_NODES) ||
			since >= REBUILD_STALE_MS) {
		rebuildTexture(center);
		m_cached_center = center;
		m_cached_view_nodes = m_view_nodes;
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
	MapCanvas *canvas = m_client->getMapCanvas();

	// Tell the canvas which window we are about to read, so the harvest's
	// player-centered eviction keeps these tiles resident even when the map
	// is focused far away from the player (pinned POI). +1 slack only: at the
	// widest zoom (1024 nodes) this gives an 11x11 hinted window, safely under
	// the canvas' resident cap.
	if (canvas) {
		const s16 radius_tiles =
			(s16)(m_view_nodes / 2 / MAPCANVAS_TILE_NODES) + 1;
		canvas->setRenderWindowHint(MapCanvas::tileCoord(center.X),
			MapCanvas::tileCoord(center.Z), radius_tiles);
	}

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

	// Pixel -> world-node mapping for the current zoom. The window spans
	// m_view_nodes across MAP_PX pixels, so a pixel offset from the center maps
	// to offset*view/MAP_PX nodes. When view < MAP_PX (zoomed in) several pixels
	// share one node (nearest upscale, crisp blocks); when view > MAP_PX several
	// nodes collapse to one pixel (sampled).
	const s32 view = m_view_nodes;
	const s32 hpx = MAP_PX / 2;
	auto pixToWorldX = [&](s32 px) -> s16 {
		return center.X + (s16)(((px - hpx) * view) / MAP_PX);
	};
	auto pixToWorldZ = [&](s32 py) -> s16 {
		// North (+Z) is up on screen, so screen-down (larger py) is smaller Z.
		return center.Z - (s16)(((py - hpx) * view) / MAP_PX);
	};

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
		const s16 wx = pixToWorldX(px);
		const s16 wz = pixToWorldZ(py);

		// Default to OPAQUE fog. Baking the fog colour straight into the texture
		// (rather than leaving fog pixels transparent and relying on a colour
		// behind the texture) makes the unexplored area a fixed colour no matter
		// what is drawn behind the map or what the time of day is. Explored
		// columns overwrite this; the rest stay fog.
		u32 out = COL_FOG.color;

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
					out = 0xFF000000u | (argb & 0x00FFFFFFu); // force opaque
					heights[py * MAP_PX + px] = cur_tile->height[ci];
				}
			}
		}

		buf[py * MAP_PX + px] = out;
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
	{
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

	// Fourth pass: feather the OUTER data edge into the fog by BLENDING terrain
	// toward COL_FOG (not by lowering alpha). The whole texture stays opaque, so
	// the unexplored area is always the fixed fog colour regardless of what is
	// drawn behind the map or the time of day. Interior holes were just patched,
	// so the only empty pixels left form the genuine loaded-area boundary; a
	// cheap two-sweep Chebyshev distance transform gives each terrain pixel its
	// distance to the nearest fog pixel, and we blend from full fog at the very
	// edge to pure terrain FOG_FEATHER pixels inside.
	{
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
		// Backward sweep: propagate from bottom/right, then blend toward fog.
		for (s32 py = MAP_PX - 1; py >= 0; py--)
		for (s32 px = MAP_PX - 1; px >= 0; px--) {
			const s32 idx = py * MAP_PX + px;
			s32 d = dist[idx];
			if (px < MAP_PX - 1) d = std::min(d, dist[idx + 1] + 1);
			if (py < MAP_PX - 1) d = std::min(d, dist[idx + MAP_PX] + 1);
			dist[idx] = d;

			if (heights[idx] != -32768 && d < FOG_FEATHER) {
				// t = 0 at the edge (full fog) .. 1 deep inside (pure terrain).
				const f32 t = (f32)d / (f32)FOG_FEATHER;
				const u32 c = buf[idx];
				const s32 r = (s32)(((c >> 16) & 0xFF) * t + COL_FOG.getRed()   * (1.0f - t));
				const s32 g = (s32)(((c >> 8)  & 0xFF) * t + COL_FOG.getGreen() * (1.0f - t));
				const s32 b = (s32)(( c        & 0xFF) * t + COL_FOG.getBlue()  * (1.0f - t));
				buf[idx] = 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
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

	// Fast pan/zoom can load many tiles between harvest sweeps (the regular
	// eviction point). If we ballooned past the resident cap, trim around our
	// own window now — the pixel loop above is done, so no cached tile
	// pointers are live and eviction is safe.
	if (canvas && canvas->overResidentCap()) {
		const s16 radius_tiles =
			(s16)(m_view_nodes / 2 / MAPCANVAS_TILE_NODES) + 1;
		canvas->evictFarTiles(MapCanvas::tileCoord(center.X),
			MapCanvas::tileCoord(center.Z), radius_tiles);
	}
}

void GUIMapElement::drawMarkers(const core::rect<s32> &rect, v3s16 center)
{
	const f32 w = (f32)rect.getWidth();
	const f32 h = (f32)rect.getHeight();
	const v2s32 origin = rect.UpperLeftCorner;
	const f32 extent = (f32)m_view_nodes;

	// Custom points (markers passed from Lua). `r` is the colour-dot radius —
	// the fallback drawn when a marker has no icon; the icon size is below.
	const s32 r = std::max<s32>(2, (s32)(POI_DOT_SCALE * w));
	// Icon edge length: the Lua-configurable size (m_icon_size, in pixels) when
	// set, else the responsive default. Shared by POI icons and the player icon.
	const s32 icon_half = m_icon_size > 0
		? std::max<s32>(2, m_icon_size / 2)
		: std::max<s32>(6, (s32)(POI_ICON_SCALE * w));
	ITextureSource *tsrc = m_client ? m_client->tsrc() : nullptr;

	// Draw a marker icon anchored by its BOTTOM-CENTRE on the marker position
	// (pin style): the bottom edge sits on the point and the icon body rises
	// above it. Marker icons are head/pin shapes whose meaningful tip is the
	// bottom, so this reads more naturally than centring the icon on the point.
	auto draw_marker_icon = [&](video::ITexture *t, s32 cx, s32 cy) {
		const core::dimension2du isz = t->getOriginalSize();
		const core::rect<s32> src(0, 0, isz.Width, isz.Height);
		const core::rect<s32> dest(cx - icon_half, cy - 2 * icon_half,
			cx + icon_half, cy);
		m_driver->draw2DImage(t, dest, src, &rect, nullptr, true);
	};

	for (const MapPoint &p : m_points) {
		const f32 fx = ((f32)p.world_pos.X - (f32)center.X) / extent + 0.5f;
		const f32 fz = 0.5f - ((f32)p.world_pos.Z - (f32)center.Z) / extent;
		// Positive test so a non-finite fx/fz (should be clamped at parse, but
		// belt-and-braces) also fails and is skipped rather than reaching the
		// out-of-range float->s32 cast below.
		if (!(fx >= 0.0f && fx <= 1.0f) || !(fz >= 0.0f && fz <= 1.0f))
			continue;

		const s32 cx = origin.X + (s32)(fx * w);
		const s32 cy = origin.Y + (s32)(fz * h);

		// Prefer an icon texture if the point carries one and the image exists.
		// isKnownSourceImage avoids generating a 1x1 dummy texture + logging an
		// error for a missing name (getTexture's behaviour); a known image then
		// returns a real, non-null texture.
		video::ITexture *icon = nullptr;
		if (!p.icon.empty() && tsrc && tsrc->isKnownSourceImage(p.icon))
			icon = tsrc->getTexture(p.icon);

		if (icon) {
			draw_marker_icon(icon, cx, cy);
		} else {
			const core::rect<s32> dest(cx - r, cy - r, cx + r, cy + r);
			m_driver->draw2DRectangle(p.color, dest, &rect);
		}
	}

	// Player marker — projected from the player's real position, so it sits at
	// the center when the map follows the player and rides the map (or clamps
	// off-edge) when the focus is pinned elsewhere. Drawn as a custom icon when
	// one is set and loads, otherwise the default black-outlined white dot.
	LocalPlayer *player = m_client ? m_client->getEnv().getLocalPlayer() : nullptr;
	if (player) {
		const v3f ppos = player->getPosition() / BS;
		const f32 fx = ((f32)ppos.X - (f32)center.X) / extent + 0.5f;
		const f32 fz = 0.5f - ((f32)ppos.Z - (f32)center.Z) / extent;
		if (fx >= 0.0f && fx <= 1.0f && fz >= 0.0f && fz <= 1.0f) {
			const s32 cx = origin.X + (s32)(fx * w);
			const s32 cy = origin.Y + (s32)(fz * h);

			video::ITexture *picon = nullptr;
			if (!m_player_icon.empty() && tsrc && tsrc->isKnownSourceImage(m_player_icon))
				picon = tsrc->getTexture(m_player_icon);

			if (picon) {
				draw_marker_icon(picon, cx, cy);
			} else {
				const s32 pr = std::max<s32>(3, (s32)(PLAYER_DOT_SCALE * w));
				const core::rect<s32> outer(cx - pr, cy - pr, cx + pr, cy + pr);
				const core::rect<s32> inner(cx - pr + 1, cy - pr + 1, cx + pr - 1, cy + pr - 1);
				m_driver->draw2DRectangle(video::SColor(255, 0, 0, 0), outer, &rect);
				m_driver->draw2DRectangle(video::SColor(255, 255, 255, 255), inner, &rect);
			}
		}
	}
}

bool GUIMapElement::OnEvent(const SEvent &event)
{
	// Phase 1: no interaction yet; let events pass through.
	return IGUIElement::OnEvent(event);
}
