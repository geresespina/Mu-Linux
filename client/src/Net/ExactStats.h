#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

// MEAMU TRAIN 11 -- EXACT STATS store: the client's copy of the
// Bounce 12 wire contract (GS: CustomExactStats.h is the sender).
//
// The GS pushes ONE snapshot of its OWN computed combat numbers on
// the F3 custom channel, subcode 0xFB (free on both sides before
// this train: GS sent FA=Rena / FC=fabricated flag; the client
// NetRecv_F3 had no 0xFB case). The client NEVER recomputes these
// numbers -- it renders what the server fights with.
//
//   Frame: C1 plain
//     [0] 0xC1  [1] 76  [2] 0xF3  [3] 0xFB
//     int32 LE fields at offsets:
//       4  PhysiDamageMin    8  PhysiDamageMax
//       12 MagicDamageMin    16 MagicDamageMax
//       20 PhysiSpeed        24 MagicSpeed
//       28 TotalMulPct       (100 = x1.00; mode multipliers only --
//                             the clan-war factor ships separately)
//       32 Defense           36 DefenseRate (DefenseSuccessRate)
//       40 AttackRate        (AttackSuccessRate -- this fork's
//                             MissCheck uses these same two rates
//                             for PvP; there is no PvP variant)
//       44 CritRateX10       48 ExcRateX10   (percent x10)
//       52 ClanBonusPct      (clan war weekly damage bonus)
//       56 DrainPctPerSec    (summed blood price)
//       60 FormPoolCur       64 FormPoolMax  (0/0 = no form up)
//       68 MoveSpeed         72 reserved (ignored)
//
// Push discipline (server side): character load, every attribute
// recalc, and a per-second diff sweep (Kokuo stacks / form pool),
// throttled to ~2/sec. The store therefore just keeps the LAST
// snapshot; Clear() runs on character reset so a stale character's
// numbers can never bleed into the next one. Until the first valid
// packet arrives, HasStats() is false and both presentations draw
// nothing -- an older GS simply shows no line (the Rena HUD rule).
namespace ExactStats {

constexpr uint8_t kPacketHead = 0xF3;
constexpr uint8_t kPacketSub  = 0xFB;
constexpr int     kPacketSize = 76;

struct Stats {
    int32_t physiMin = 0, physiMax = 0;
    int32_t magicMin = 0, magicMax = 0;
    int32_t physiSpeed = 0, magicSpeed = 0;
    int32_t totalMulPct = 100;
    int32_t defense = 0, defenseRate = 0, attackRate = 0;
    int32_t critX10 = 0, excX10 = 0;
    int32_t clanBonusPct = 0;
    int32_t drainPct = 0;
    int32_t formCur = 0, formMax = 0;
    int32_t moveSpeed = 0;
};

// Pure parser (header-inline so a proof harness exercises this
// exact code): validates the frame and extracts every field.
// Any malformation returns false and leaves `out` untouched.
inline bool ParsePacket(const uint8_t* msg, int size, Stats& out)
{
    if (!msg || size < kPacketSize) return false;
    if (msg[0] != 0xC1 || msg[2] != kPacketHead || msg[3] != kPacketSub) return false;
    auto rd = [&](int off) -> int32_t {
        return (int32_t)((uint32_t)msg[off] | ((uint32_t)msg[off + 1] << 8) |
                         ((uint32_t)msg[off + 2] << 16) | ((uint32_t)msg[off + 3] << 24));
    };
    Stats s;
    s.physiMin = rd(4);   s.physiMax = rd(8);
    s.magicMin = rd(12);  s.magicMax = rd(16);
    s.physiSpeed = rd(20); s.magicSpeed = rd(24);
    s.totalMulPct = rd(28);
    s.defense = rd(32);   s.defenseRate = rd(36); s.attackRate = rd(40);
    s.critX10 = rd(44);   s.excX10 = rd(48);
    s.clanBonusPct = rd(52);
    s.drainPct = rd(56);
    s.formCur = rd(60);   s.formMax = rd(64);
    s.moveSpeed = rd(68);
    out = s;
    return true;
}

// F3/FB receive handler (wired in NetRecv_F3, Recv_Character.cpp).
void Receive(const uint8_t* msg, int size);

// Character reset (CNetwork::ResetCharacterData).
void Clear();

bool HasStats();
const Stats& Get();

// Full-sheet visibility (Train 11): toggled by F6 or by clicking
// the slim line; rendering lives in UI/ExactStatsHud.cpp.
bool SheetOpen();
void ToggleSheet();
void CloseSheet();

} // namespace ExactStats
