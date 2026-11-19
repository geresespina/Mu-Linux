#include "stdafx.h"
#include "Net/ExactStats.h"

// MEAMU TRAIN 11 -- EXACT STATS store. See Net/ExactStats.h for the
// wire contract. Deliberately dumb: the last valid snapshot stands
// until the next one or Clear(); the server owns freshness (load +
// recalc pushes + a per-second diff sweep).

namespace ExactStats {

namespace {
Stats gStats;
bool  gHave = false;
bool  gSheetOpen = false;
}

void Receive(const uint8_t* msg, int size)
{
    Stats s;
    if (!ParsePacket(msg, size, s)) return; // malformed: keep the last truth
    gStats = s;
    gHave = true;
}

void Clear()
{
    gStats = Stats();
    gHave = false;
    gSheetOpen = false;
}

bool HasStats() { return gHave; }

const Stats& Get() { return gStats; }

bool SheetOpen() { return gSheetOpen; }

void ToggleSheet() { gSheetOpen = !gSheetOpen; }

void CloseSheet() { gSheetOpen = false; }

} // namespace ExactStats
