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
// node is found within [y_top, y_bottom]. Returns false for an empty/unloaded
// column, OR when the topmost solid node has no confirmed-loaded air directly
// above it (a still-streaming surface — see air_above in the body), so a deeper
// layer is never frozen into the persistent canvas. Used by the canvas harvest;
// the live map view reads the baked canvas, not this. The returned colour is
// plain (no cave tint) — tinting is a render-time concern.
bool scanSurfaceColumn(Map &map, const NodeDefManager *ndef, s16 wx, s16 wz,
		s16 y_top, s16 y_bottom, u32 &out_argb, s16 &out_height);

// Persistent, world-anchored, chunked record of explored surface terrain for one
// world. Owned by Client so it accumulates across the whole session even while
// the map UI is closed, and survives relog via on-disk tiles.
//
// Storage location is chosen by the caller and passed in as a directory: for a
// local (internal-server) world it lives inside the world folder, so the
// fog-of-war is deleted, moved and copied together with the world; for a remote
// server (no local world folder) the caller passes a client-side cache dir.
//
// All access is main-thread only (Client::step / GUIMapElement::draw /
// Client::Stop all run on the client main thread), so there is no locking.
class MapCanvas
{
public:
	// `dir` is the directory under which tile files (t_<tx>_<tz>.bin) are stored.
	// Created lazily on the first save.
	explicit MapCanvas(std::string dir);
	~MapCanvas();

	// Record an explored surface cell at world (wx, wz). No-op semantics for
	// callers: only call when a real surface was found — never write fog.
	void setCell(s16 wx, s16 wz, u32 argb, s16 height);

	// Return the resident tile for tile-coords (tx, tz), lazy-loading it from
	// disk on a cache miss. Returns nullptr if the tile was never explored
	// (no file on disk).
	//
	// POINTER VALIDITY: setCell / findTileReadonly never evict, so returned
	// pointers stay valid until the next evictFarTiles() call. Callers that
	// cache tile pointers across a scan (the harvest sweep, the map render)
	// must only call evictFarTiles() after the scan is done.
	const MapTile *findTileReadonly(s16 tx, s16 tz);

	// Evict resident tiles whose Chebyshev distance (in tiles) from
	// (center_tx, center_tz) exceeds keep_radius_tiles, saving dirty ones.
	// Tiles inside a recently announced render window (setRenderWindowHint)
	// are kept too. If the resident count still exceeds the hard cap
	// afterwards, evict the farthest tiles down to the cap. Call OUTSIDE any
	// scan that caches tile pointers (see findTileReadonly). This is the ONLY
	// eviction point: the old per-insert LRU eviction could free a tile
	// mid-sweep while the harvest still held a pointer to it (use-after-free)
	// and thrashed the cache once the resident count crossed the cap after
	// several teleports.
	void evictFarTiles(s16 center_tx, s16 center_tz, s16 keep_radius_tiles);

	// Announce the window the map UI is currently rendering (tile coords +
	// radius), so the player-centered eviction keeps those tiles resident even
	// when the map is focused far from the player. Cleared explicitly when the
	// map element goes away; the TTL is only a leak backstop in case it never
	// gets the chance.
	void setRenderWindowHint(s16 center_tx, s16 center_tz, s16 radius_tiles);
	void clearRenderWindowHint() { m_hint_radius = 0; }

	// Flush dirty tiles to disk no more often than the internal interval.
	void maybeFlush(f32 dtime);
	// Flush every dirty tile now (call on shutdown).
	void flushAll();

	// Number of tiles currently resident in RAM (profiling/diagnostics).
	size_t residentTileCount() const { return m_tiles.size(); }

	// True when the resident count exceeds the hard cap — i.e. an eviction
	// pass is overdue (see evictFarTiles).
	bool overResidentCap() const;

	// Convert a world node coord to its tile coord (floor division).
	static s16 tileCoord(s16 w);

private:
	MapTile *getOrCreateTile(s16 tx, s16 tz);
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

	std::string m_dir;                 // caller-provided tile dir (see Client::getMapCanvas)
	bool m_dir_ready = false;          // created lazily on first save
	std::unordered_map<v2s16, std::unique_ptr<MapTile>, V2s16Hash> m_tiles;
	f32 m_flush_timer = 0.0f;

	// Render-window keep hint (see setRenderWindowHint).
	v2s16 m_hint_center{0, 0};
	s16 m_hint_radius = 0;
	u64 m_hint_set_ms = 0;
};
