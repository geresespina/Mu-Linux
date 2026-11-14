// MapTrack.cpp — see MapTrack.h for the design. The table is a
// wholesale-replaced snapshot: every F3/F6 packet carries the full
// list for one map, so there is no incremental state to corrupt; a
// missed packet self-heals one second later, and a stale table
// (> 3000 ms) simply stops drawing.

#include "stdafx.h"
#include "Net/MapTrack.h"
#include "Net/Recv/NetRecv.h"
#include "Platform/ITimer.h"

#include <cstdlib>
#include <cstring>

namespace {

constexpr int kMaxEntries = 160;
constexpr uint32_t kFreshMs = 3000;

MapTrackEntry s_Entries[kMaxEntries];
int s_Count = 0;
int s_Map = -1;
uint32_t s_RecvTick = 0;
bool s_HaveTable = false;

const char* const kBijuNames[10] = {
    "Shukaku",   // 155
    "Matatabi",  // 156
    "Isobu",     // 157
    "Son Goku",  // 158
    "Kokuo",     // 159
    "Saiken",    // 160
    "Chomei",    // 161
    "Gyuki",     // 162
    "Kurama",    // 163
    "Ten-Tails", // 164
};

// Parses one wire packet into the store. Returns false (and leaves
// the previous table untouched) on any malformation.
bool ParseAndStore(const uint8_t* msg, int size)
{
    // C2 sizeHi sizeLo F3 F6 map count entries...
    if (!msg || size < 7) return false;
    if (msg[0] != 0xC2 || msg[3] != 0xF3 || msg[4] != 0xF6) return false;
    const int declared = (msg[1] << 8) | msg[2];
    if (declared != size) return false;

    const int map = msg[5];
    const int count = msg[6];
    if (count > kMaxEntries) return false;

    MapTrackEntry parsed[kMaxEntries];
    int pos = 7;
    for (int i = 0; i < count; ++i) {
        if (pos + 5 > size) return false;
        MapTrackEntry& e = parsed[i];
        e.Category = msg[pos];
        e.X = msg[pos + 1];
        e.Y = msg[pos + 2];
        e.Aux = msg[pos + 3];
        const int nameLen = msg[pos + 4];
        pos += 5;
        if (e.Category > MapTrackBiju) return false;
        if (nameLen > 10 || pos + nameLen > size) return false;
        std::memset(e.Name, 0, sizeof(e.Name));
        if (nameLen > 0) std::memcpy(e.Name, msg + pos, nameLen);
        pos += nameLen;
        if (e.Category == MapTrackBiju && (e.Aux < 155 || e.Aux > 164)) return false;
    }
    if (pos != size) return false;

    std::memcpy(s_Entries, parsed, sizeof(MapTrackEntry) * count);
    s_Count = count;
    s_Map = map;
    s_RecvTick = Platform::GetTimer().Ticks();
    s_HaveTable = true;
    return true;
}

// MU_MAPTRACK_SELFTEST=1: feeds a synthetic packet through the real
// parser at first receive, verifies the snapshot + dedup inputs,
// then clears. Dev-only proof of the parse/classify path (the same
// env-gate style as MU_PAD_SELFTEST / MU_KB_SELFTEST).
void RunSelfTestOnce()
{
    static bool done = false;
    if (done) return;
    done = true;
    if (!std::getenv("MU_MAPTRACK_SELFTEST")) return;

    // map 3, 3 entries: human Tester @(10,20), phantom PhantomX
    // @(30,40), Biju class 163 @(50,60) — built byte by byte.
    uint8_t pkt[64];
    int pos = 0;
    pkt[pos++] = 0xC2; pkt[pos++] = 0; pkt[pos++] = 0; // size patched below
    pkt[pos++] = 0xF3; pkt[pos++] = 0xF6;
    pkt[pos++] = 3;    // map
    pkt[pos++] = 3;    // count
    pkt[pos++] = MapTrackHuman; pkt[pos++] = 10; pkt[pos++] = 20; pkt[pos++] = 0; pkt[pos++] = 6;
    std::memcpy(pkt + pos, "Tester", 6); pos += 6;
    pkt[pos++] = MapTrackPhantom; pkt[pos++] = 30; pkt[pos++] = 40; pkt[pos++] = 0; pkt[pos++] = 8;
    std::memcpy(pkt + pos, "PhantomX", 8); pos += 8;
    pkt[pos++] = MapTrackBiju; pkt[pos++] = 50; pkt[pos++] = 60; pkt[pos++] = 163; pkt[pos++] = 0;
    pkt[1] = (uint8_t)(pos >> 8);
    pkt[2] = (uint8_t)(pos & 0xFF);

    bool ok = ParseAndStore(pkt, pos);
    MapTrackEntry out[8];
    const int n = ok ? MapTrack::Snapshot(3, out, 8) : -1;
    ok = ok && n == 3
        && out[0].Category == MapTrackHuman && out[0].X == 10 && out[0].Y == 20
        && std::strcmp(out[0].Name, "Tester") == 0
        && out[1].Category == MapTrackPhantom && out[1].X == 30 && out[1].Y == 40
        && std::strcmp(out[1].Name, "PhantomX") == 0
        && out[2].Category == MapTrackBiju && out[2].Aux == 163 && out[2].X == 50 && out[2].Y == 60
        && MapTrack::Snapshot(4, out, 8) == 0
        && MapTrack::BijuName(163) && std::strcmp(MapTrack::BijuName(163), "Kurama") == 0
        && MapTrack::BijuName(154) == nullptr;
    NetLog("NET:  MapTrack SELFTEST %s (entries=%d)", ok ? "PASS" : "FAIL", n);
    MapTrack::Clear();
}

} // namespace

namespace MapTrack {

void Receive(const uint8_t* msg, int size)
{
    RunSelfTestOnce();
    if (!ParseAndStore(msg, size)) {
        NetLog("NET:  -> F3/F6 MapTrack malformed pkt (Size=%d) — dropped", size);
    }
}

int Snapshot(int currentMap, MapTrackEntry* out, int cap)
{
    if (!s_HaveTable || !out || cap <= 0) return 0;
    if (s_Map != currentMap) return 0;
    if (Platform::GetTimer().Ticks() - s_RecvTick > kFreshMs) return 0;
    const int n = s_Count < cap ? s_Count : cap;
    std::memcpy(out, s_Entries, sizeof(MapTrackEntry) * n);
    return n;
}

void Clear()
{
    s_HaveTable = false;
    s_Count = 0;
    s_Map = -1;
}

const char* BijuName(int monsterClass)
{
    if (monsterClass < 155 || monsterClass > 164) return nullptr;
    return kBijuNames[monsterClass - 155];
}

} // namespace MapTrack

// F3/F6 dispatch entry (NetRecv_F3 in Recv_Character.cpp) — the
// same forward-declared free-function pattern as Recv_PetSquad
// (F3/F5).
void MapTrack_RecvF6(const BYTE* Msg, int Size)
{
    MapTrack::Receive((const uint8_t*)Msg, Size);
}
