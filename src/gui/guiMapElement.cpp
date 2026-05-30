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
#include "client/node_visuals.h"
#include "client/texturesource.h"
#include "constants.h"
#include "map.h"
#include "mapblock.h"
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

// Vertical scan window around the player's Y when looking for a surface node.
// SCAN_DOWN is deliberately deep so that flying high still finds the ground far
// below; unloaded blocks in that range are skipped 16 nodes at a time (see the
// block-level skip in rebuildTexture), so a wide window stays cheap.
constexpr s16 SCAN_UP = 64;
constexpr s16 SCAN_DOWN = 400;

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

// NOTE (perf, Phase 2): this bakes the whole texture synchronously on the
// render thread (scan + 4 image passes + GPU upload). It is throttled so it
// runs at most a few times per second, but a rebuild can still cause a small
// hitch on low-end mobile. The engine minimap avoids this with a dedicated
// MinimapUpdateThread; moving this work off-thread is deferred to the Phase 2
// fog-of-war rework, where the data source changes anyway.
void GUIMapElement::rebuildTexture(v3s16 center)
{
	const NodeDefManager *ndef = m_client->getNodeDefManager();
	Map &map = m_client->getEnv().getMap();

	video::IImage *img = m_driver->createImage(video::ECF_A8R8G8B8,
		core::dimension2du(MAP_PX, MAP_PX));
	if (!img)
		return;

	// Height per pixel for relief shading (INT16_MIN = no surface found).
	std::vector<s16> heights(MAP_PX * MAP_PX, -32768);

	const s32 half = MAP_EXTENT_NODES / 2;

	const s16 y_top = center.Y + SCAN_UP;
	const s16 y_bottom = center.Y - SCAN_DOWN;

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

	// First pass: pick the topmost non-air node per column and store its colour.
	for (s32 py = 0; py < MAP_PX; py++)
	for (s32 px = 0; px < MAP_PX; px++) {
		// North (+Z) is up on screen, East (+X) is right.
		const s16 wx = center.X + (s16)(px * NODES_PER_PIXEL - half);
		const s16 wz = center.Z + (s16)(half - py * NODES_PER_PIXEL);

		video::SColor out(0, 0, 0, 0); // transparent => fog shows through
		bool found = false;

		// Scan the column top-down. Cache the current MapBlock so we don't do a
		// full block lookup per node, and skip whole unloaded blocks at once —
		// this is what makes a deep SCAN_DOWN affordable.
		MapBlock *block = nullptr;
		v3s16 cached_bp(-32768, -32768, -32768);

		for (s16 wy = y_top; wy >= y_bottom; wy--) {
			const v3s16 wp(wx, wy, wz);
			const v3s16 bp = getNodeBlockPos(wp);
			if (bp != cached_bp) {
				cached_bp = bp;
				block = map.getBlockNoCreateNoEx(bp);
			}
			if (!block) {
				// Jump straight to the node below this block's bottom.
				wy = bp.Y * MAP_BLOCKSIZE; // loop's wy-- lands at bottom-1
				continue;
			}

			MapNode n = block->getNodeNoCheck(wp - bp * MAP_BLOCKSIZE);
			content_t c = n.getContent();
			if (c == CONTENT_IGNORE || c == CONTENT_AIR)
				continue;
			const ContentFeatures &f = ndef->get(c);
			if (f.drawtype == NDT_AIRLIKE)
				continue;

			// Representative top colour of the node (same recipe the engine
			// uses for the minimap pixel — but computed here independently).
			video::SColor tilecolor(255, 255, 255, 255);
			const TileDef &tile = f.tiledef[0];
			const TileDef &overlay = f.tiledef_overlay[0];
			if (!overlay.name.empty() && overlay.has_color) {
				tilecolor = overlay.color;
			} else if (overlay.name.empty() && tile.has_color) {
				tilecolor = tile.color;
			} else if (f.visuals) {
				f.visuals->getColor(n.param2, &tilecolor);
			}
			if (f.visuals) {
				const video::SColor &mc = f.visuals->minimap_color;
				tilecolor.setRed(tilecolor.getRed() * mc.getRed() / 255);
				tilecolor.setGreen(tilecolor.getGreen() * mc.getGreen() / 255);
				tilecolor.setBlue(tilecolor.getBlue() * mc.getBlue() / 255);
			}
			// Cave view: tint nodes below the player toward the cave tone and
			// darken them with depth, so descending reads as going deeper.
			if (underground && wy < center.Y) {
				const f32 t = core::clamp(
					(f32)(center.Y - wy) / CAVE_DEPTH_FULL, 0.0f, 1.0f);
				const f32 keep = 1.0f - 0.6f * t; // fade material colour out
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
			break;
		}

		// In cave view, empty columns (no surface within the scan) get a soft
		// cave fill instead of falling through to the near-black fog.
		if (!found && underground)
			out = COL_CAVE;

		img->setPixel(px, py, out);
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

		video::SColor c = img->getPixel(px, py);
		c.setRed(core::clamp((s32)(c.getRed() * factor), 0, 255));
		c.setGreen(core::clamp((s32)(c.getGreen() * factor), 0, 255));
		c.setBlue(core::clamp((s32)(c.getBlue() * factor), 0, 255));
		img->setPixel(px, py, c);
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
				img->setPixel(px, py,
					img->getPixel(src_idx % MAP_PX, src_idx / MAP_PX));
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
				video::SColor c = img->getPixel(px, py);
				c.setAlpha((u32)(255 * d / FOG_FEATHER));
				img->setPixel(px, py, c);
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
