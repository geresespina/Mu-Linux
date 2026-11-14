#pragma once

#include <cstdint>

// MapTrack — client half of the MEAMU whole-map tracker (2026-11-13,
// G: "Biju and Human players should show in Map (using Tab)" +
// "EVERYTHING tracked across the whole map even out of sight").
//
// The server feeds, once per second, the track list of the hero's
// current map as one custom packet C2 F3 F6 (GS module
// CustomMapTrack; spec in builds/map-markers-2026-11-13/
// PACKET-SPEC.md): every real player, every phantom, and every live
// world Biju on the map — including the ones far outside the
// viewport pool. This module stores the latest table; CMiniMap
// renders it across the whole Tab map, deduplicated against the
// near-range viewport markers. Normal monsters / NPCs are NOT fed
// (deliberate — thousands of objects; viewport dots cover them).

// Wire categories (mirror the GS module).
enum MapTrackCategory : uint8_t {
    MapTrackHuman = 0,
    MapTrackPhantom = 1,
    MapTrackBiju = 2,
};

struct MapTrackEntry {
    uint8_t Category;   // MapTrackCategory
    uint8_t X;          // tile coords on the map
    uint8_t Y;
    uint8_t Aux;        // Biju: monster class 155..164; else 0
    char Name[11];      // empty for Biju (name resolves from Aux)
};

namespace MapTrack {

// F3/F6 receive handler (wired in NetRecv_F3). Validates and stores
// the table wholesale; malformed packets are dropped.
void Receive(const uint8_t* msg, int size);

// Copies the current table into `out` when it belongs to
// `currentMap` and is fresh (received <= 3000 ms ago). Returns the
// entry count, 0 when there is nothing current to draw.
int Snapshot(int currentMap, MapTrackEntry* out, int cap);

// Drops the stored table (map changes are also handled by the map
// check in Snapshot; this is the explicit reset).
void Clear();

// Canon display name for a Biju monster class (155..164), or
// nullptr for any other class.
const char* BijuName(int monsterClass);

} // namespace MapTrack
