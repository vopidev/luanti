// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 VOPI Team

#pragma once

#include "irrlichttypes_bloated.h"
#include <memory>
#include <string>
#include <unordered_map>

class Map;
class NodeDefManager;

// Sentinel height stored for cells that have no recorded surface (unexplored).
// Matches the value GUIMapElement uses for its per-pixel height vector.
constexpr s16 MAPCANVAS_NO_HEIGHT = -32768;

// Side length, in world nodes, of one persistent canvas tile. Power of two so
// the in-tile offset is a cheap bitmask. 128*128 cells * (4B colour + 2B
// height) = 96 KiB raw per tile.
constexpr s32 MAPCANVAS_TILE_NODES = 128;

// One world-anchored tile of explored surface data (struct-of-arrays so each
// array is contiguous for fast zstd and trivial (de)serialisation).
struct MapTile
{
	static constexpr s32 CELLS = MAPCANVAS_TILE_NODES * MAPCANVAS_TILE_NODES;

	u32 colour[CELLS];  // ARGB8888; alpha == 0 means "never explored" (fog)
	s16 height[CELLS];  // surface height; MAPCANVAS_NO_HEIGHT means unexplored

	bool dirty = false;       // written since last disk flush
	u64 last_used_ms = 0;     // for LRU eviction

	MapTile()
	{
		for (s32 i = 0; i < CELLS; i++) {
			colour[i] = 0;                    // alpha 0 => unexplored
			height[i] = MAPCANVAS_NO_HEIGHT;
		}
	}
};

// Scan a single world column top-down for the representative surface colour and
// height, using the same recipe as the engine minimap (topmost non-air node,
// tile/overlay colour or palette colour, multiplied by the node's
// minimap_color). Returns true and fills out_argb / out_height when a surface
// node is found within [y_top, y_bottom]; returns false for an empty/unloaded
// column. Shared by the live render path and the persistent-canvas harvest so
// both agree on colour. The returned colour is plain (no cave tint) — tinting
// is a render-time concern.
bool scanSurfaceColumn(Map &map, const NodeDefManager *ndef, s16 wx, s16 wz,
		s16 y_top, s16 y_bottom, u32 &out_argb, s16 &out_height);

// Persistent, world-anchored, chunked record of explored surface terrain for one
// world (keyed by map seed). Owned by Client so it accumulates across the whole
// session even while the map UI is closed, and survives relog via on-disk tiles.
//
// All access is main-thread only (Client::step / GUIMapElement::draw /
// Client::Stop all run on the client main thread), so there is no locking.
class MapCanvas
{
public:
	explicit MapCanvas(u64 seed);
	~MapCanvas();

	// Record an explored surface cell at world (wx, wz). No-op semantics for
	// callers: only call when a real surface was found — never write fog.
	void setCell(s16 wx, s16 wz, u32 argb, s16 height);

	// Return the resident tile for tile-coords (tx, tz), lazy-loading it from
	// disk on a cache miss. Returns nullptr if the tile was never explored
	// (no file on disk). The pointer is valid until the next setCell /
	// findTileReadonly that could trigger eviction, so use it immediately.
	const MapTile *findTileReadonly(s16 tx, s16 tz);

	// Flush dirty tiles to disk no more often than the internal interval.
	void maybeFlush(f32 dtime);
	// Flush every dirty tile now (call on shutdown).
	void flushAll();

	// Convert a world node coord to its tile coord (floor division).
	static s16 tileCoord(s16 w);

private:
	MapTile *getOrCreateTile(s16 tx, s16 tz);
	void touch(MapTile *t);
	void evictIfNeeded();
	bool loadTile(s16 tx, s16 tz, MapTile &out) const;
	void saveTile(s16 tx, s16 tz, const MapTile &t) const;
	std::string tilePath(s16 tx, s16 tz) const;

	struct V2s16Hash
	{
		size_t operator()(const v2s16 &p) const
		{
			return ((size_t)(u16)p.X << 16) ^ (u16)p.Y;
		}
	};

	u64 m_seed;
	std::string m_dir;                 // path_user/client/worldmaps/<seed>/
	bool m_dir_ready = false;          // created lazily on first save
	std::unordered_map<v2s16, std::unique_ptr<MapTile>, V2s16Hash> m_tiles;
	f32 m_flush_timer = 0.0f;
};
