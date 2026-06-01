// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 VOPI Team

#include "mapCanvas.h"

#include <sstream>
#include "client/node_visuals.h"
#include "constants.h"
#include "filesys.h"
#include "log.h"
#include "map.h"
#include "mapblock.h"
#include "mapnode.h"
#include "nodedef.h"
#include "porting.h"
#include "serialization.h"
#include "util/numeric.h"
#include "util/serialize.h"

namespace {

// In-RAM resident-tile cap. 64 * 96 KiB = ~6 MiB; a render window touches <=9
// tiles, so this is generous headroom and eviction rarely fires.
constexpr size_t MAX_RESIDENT_TILES = 64;

// Flush dirty tiles to disk at most this often (seconds).
constexpr f32 FLUSH_INTERVAL_S = 10.0f;

// On-disk tile header.
const char TILE_MAGIC[4] = {'K', 'C', 'M', 'T'};
constexpr u8 TILE_VERSION = 1;

// Raw (uncompressed) payload size: colour[] as u32 + height[] as s16.
constexpr size_t PAYLOAD_BYTES =
	(size_t)MapTile::CELLS * 4 + (size_t)MapTile::CELLS * 2;

} // namespace

// ---------------------------------------------------------------------------
// Column-scan helper (used by the persistent-canvas harvest)
// ---------------------------------------------------------------------------

bool scanSurfaceColumn(Map &map, const NodeDefManager *ndef, s16 wx, s16 wz,
		s16 y_top, s16 y_bottom, u32 &out_argb, s16 &out_height)
{
	// Scan top-down, caching the current MapBlock so we don't do a full block
	// lookup per node, and skipping whole unloaded blocks in one jump.
	MapBlock *block = nullptr;
	v3s16 cached_bp(-32768, -32768, -32768);

	// Whether the node directly above the current scan position is confirmed
	// LOADED empty space (real air). The surface we finally pick is trustworthy
	// only if loaded air sits directly above it — that proves nothing solid is
	// hidden higher up. If we instead reached a node by jumping over an unloaded
	// or ungenerated block, a real (higher) surface may still be streaming in
	// there and the node we found is likely a DEEPER layer (e.g. red sandstone
	// under desert sand). Recording that would freeze a wrong colour into the
	// persistent canvas (skip-recorded never revisits it), so we bail and let a
	// later sweep record the column once it is fully loaded.
	bool air_above = false;

	for (s16 wy = y_top; wy >= y_bottom; wy--) {
		const v3s16 wp(wx, wy, wz);
		const v3s16 bp = getNodeBlockPos(wp);
		if (bp != cached_bp) {
			cached_bp = bp;
			block = map.getBlockNoCreateNoEx(bp);
		}
		if (!block) {
			wy = bp.Y * MAP_BLOCKSIZE; // loop's wy-- lands at this block's bottom-1
			air_above = false;         // jumped an unloaded block: above is not loaded
			continue;
		}

		MapNode n = block->getNodeNoCheck(wp - bp * MAP_BLOCKSIZE);
		content_t c = n.getContent();
		if (c == CONTENT_AIR) {
			air_above = true;          // real, loaded air above
			continue;
		}
		if (c == CONTENT_IGNORE) {
			air_above = false;         // ungenerated node: treat like unloaded
			continue;
		}

		const ContentFeatures &f = ndef->get(c);
		if (f.drawtype == NDT_AIRLIKE) {
			air_above = true;
			continue;
		}

		// Topmost solid node. Trust it as the surface only if confirmed-loaded
		// air sits directly above; otherwise the true surface may be hidden in an
		// unloaded block overhead and this is a deeper layer — skip, retry later.
		if (!air_above)
			return false;

		// Representative top colour of the node (same recipe as the minimap).
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
		tilecolor.setAlpha(255); // explored cells are always opaque (not fog)

		out_argb = tilecolor.color;
		out_height = wy;
		return true;
	}

	return false;
}

// ---------------------------------------------------------------------------
// MapCanvas
// ---------------------------------------------------------------------------

MapCanvas::MapCanvas(u64 seed)
{
	m_dir = porting::path_user + DIR_DELIM + "client" + DIR_DELIM +
		"worldmaps" + DIR_DELIM + std::to_string(seed);
}

MapCanvas::~MapCanvas()
{
	flushAll();
}

s16 MapCanvas::tileCoord(s16 w)
{
	return getContainerPos(w, (s16)MAPCANVAS_TILE_NODES);
}

std::string MapCanvas::tilePath(s16 tx, s16 tz) const
{
	return m_dir + DIR_DELIM + "t_" + std::to_string(tx) + "_" +
		std::to_string(tz) + ".bin";
}

void MapCanvas::touch(MapTile *t)
{
	t->last_used_ms = porting::getTimeMs();
}

void MapCanvas::setCell(s16 wx, s16 wz, u32 argb, s16 height)
{
	const s16 tx = tileCoord(wx);
	const s16 tz = tileCoord(wz);
	MapTile *t = getOrCreateTile(tx, tz);

	const s32 ox = wx & (MAPCANVAS_TILE_NODES - 1);
	const s32 oz = wz & (MAPCANVAS_TILE_NODES - 1);
	const s32 idx = oz * MAPCANVAS_TILE_NODES + ox;

	t->colour[idx] = argb;
	t->height[idx] = height;
	t->dirty = true;
	touch(t);
}

const MapTile *MapCanvas::findTileReadonly(s16 tx, s16 tz)
{
	auto it = m_tiles.find(v2s16(tx, tz));
	if (it != m_tiles.end()) {
		touch(it->second.get());
		return it->second.get();
	}

	// Cold miss. Cheaply check the file exists BEFORE allocating a 96 KiB tile:
	// most lookups for unexplored area miss, and a 96 KiB alloc + memset per
	// miss is exactly what made the render assemble pass expensive.
	if (!fs::PathExists(tilePath(tx, tz)))
		return nullptr;

	auto tile = std::make_unique<MapTile>();
	if (!loadTile(tx, tz, *tile))
		return nullptr;

	touch(tile.get());
	const MapTile *raw = tile.get();
	m_tiles[v2s16(tx, tz)] = std::move(tile);
	evictIfNeeded();
	return raw;
}

MapTile *MapCanvas::getOrCreateTile(s16 tx, s16 tz)
{
	auto it = m_tiles.find(v2s16(tx, tz));
	if (it != m_tiles.end())
		return it->second.get();

	auto tile = std::make_unique<MapTile>();
	// Seed from disk if this tile was explored in a previous session, so we
	// extend rather than overwrite saved progress.
	loadTile(tx, tz, *tile);

	MapTile *raw = tile.get();
	m_tiles[v2s16(tx, tz)] = std::move(tile);
	evictIfNeeded();
	return raw;
}

void MapCanvas::evictIfNeeded()
{
	if (m_tiles.size() <= MAX_RESIDENT_TILES)
		return;

	// Find the least-recently-used tile (linear scan is fine at this size).
	auto lru = m_tiles.end();
	u64 oldest = U64_MAX;
	for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
		if (it->second->last_used_ms < oldest) {
			oldest = it->second->last_used_ms;
			lru = it;
		}
	}
	if (lru == m_tiles.end())
		return;

	if (lru->second->dirty)
		saveTile(lru->first.X, lru->first.Y, *lru->second);
	m_tiles.erase(lru);
}

void MapCanvas::maybeFlush(f32 dtime)
{
	m_flush_timer += dtime;
	if (m_flush_timer < FLUSH_INTERVAL_S)
		return;
	m_flush_timer = 0.0f;
	flushAll();
}

void MapCanvas::flushAll()
{
	for (auto &entry : m_tiles) {
		if (entry.second->dirty) {
			saveTile(entry.first.X, entry.first.Y, *entry.second);
			entry.second->dirty = false;
		}
	}
}

void MapCanvas::saveTile(s16 tx, s16 tz, const MapTile &t) const
{
	if (!m_dir_ready) {
		if (!fs::CreateAllDirs(m_dir)) {
			warningstream << "MapCanvas: cannot create dir " << m_dir << std::endl;
			return;
		}
		const_cast<MapCanvas *>(this)->m_dir_ready = true;
	}

	// Build raw payload: colour[] then height[], big-endian (network byte
	// order) via writeU32/writeS16 — read back symmetrically by loadTile.
	std::string raw;
	raw.resize(PAYLOAD_BYTES);
	char *p = raw.data();
	for (s32 i = 0; i < MapTile::CELLS; i++) {
		writeU32((u8 *)p, t.colour[i]);
		p += 4;
	}
	for (s32 i = 0; i < MapTile::CELLS; i++) {
		writeS16((u8 *)p, t.height[i]);
		p += 2;
	}

	std::ostringstream oss(std::ios::binary);
	oss.write(TILE_MAGIC, 4);
	oss.put((char)TILE_VERSION);
	u8 hdr[2];
	writeS16(hdr, tx); oss.write((char *)hdr, 2);
	writeS16(hdr, tz); oss.write((char *)hdr, 2);
	compressZstd(raw, oss);

	if (!fs::safeWriteToFile(tilePath(tx, tz), oss.str()))
		warningstream << "MapCanvas: failed to write tile " << tx << "," << tz << std::endl;
}

bool MapCanvas::loadTile(s16 tx, s16 tz, MapTile &out) const
{
	const std::string path = tilePath(tx, tz);
	if (!fs::PathExists(path))
		return false;

	std::string data;
	if (!fs::ReadFile(path, data) || data.size() < 9)
		return false;
	if (memcmp(data.data(), TILE_MAGIC, 4) != 0 || (u8)data[4] != TILE_VERSION) {
		warningstream << "MapCanvas: bad header in " << path << std::endl;
		return false;
	}

	// Decompress the payload that follows the 9-byte header.
	std::istringstream iss(data.substr(9), std::ios::binary);
	std::ostringstream raw(std::ios::binary);
	try {
		decompressZstd(iss, raw);
	} catch (...) {
		warningstream << "MapCanvas: decompress failed for " << path << std::endl;
		return false;
	}
	const std::string payload = raw.str();
	if (payload.size() != PAYLOAD_BYTES) {
		warningstream << "MapCanvas: bad payload size in " << path << std::endl;
		return false;
	}

	const u8 *p = (const u8 *)payload.data();
	for (s32 i = 0; i < MapTile::CELLS; i++) {
		out.colour[i] = readU32(p);
		p += 4;
	}
	for (s32 i = 0; i < MapTile::CELLS; i++) {
		out.height[i] = readS16(p);
		p += 2;
	}
	return true;
}
