#pragma once
// FabricatedMark.h — MEAMU (Train 10 + Bounce 11; G's rulings 2026-11-19:
// no name labels floating over follower bodies, and Space must hunt in
// the Stadium, which is phantom country).
//
// Fabricated player-kind bodies — phantoms/KAGE, companions, clan
// summons, tame followers, Akatsuki roamers/ambushers, disco dancers —
// are spawned by the GS as OBJECT_USER, so they arrive through the
// player viewport packet bit-identical to real players (see the "NOT
// covered, by design" note in OwnedSummonMark.h). Bounce 11 closes
// that gap on the wire: right after the viewport record(s) that
// create/refresh such a body, the GS sends a C1:F3:FC note carrying
// the body's fabricated kind and, for followers, its owner:
//
//   +0..3  {C1, 9, 0xF3, 0xFC}
//   +4,+5  index[2]  (the body's key, SET_NUMBERHB/LB)
//   +6     kind      (1 = ARMY, 2 = FOLLOWER)
//   +7,+8  owner[2]  (owner key for followers; 0xFFFF = none)
//
// This mark records what the note said, per pool slot:
//
//   Set   — Recv_FabricatedFlag (NetRecv_F3 case 0xFC) only.
//   Clear — CreateCharacterPointer, beside OwnedSummonMark::Clear, so
//           a recycled slot never inherits the mark.
//   Is/Kind/Owner — HumanLabels skips FOLLOWER bodies (army bodies
//           keep today's label behavior — they are the war's
//           "players"); the KbPlay Space latch accepts fabricated
//           bodies as legal targets, except the hero's own followers
//           (kind == FOLLOWER && owner == hero key). Real players are
//           never marked: no note is ever sent for them, so they stay
//           manual-only targets, by construction.
//
// Pairing: with an old GS the note never arrives and nothing is ever
// marked (Train 9 behavior); with an old client the note is ignored
// by the F3 default arm. The table + functions live in
// Recv_Viewport.cpp beside OwnedSummonMark; no new translation unit.

namespace FabricatedMark {

constexpr int kNone = 0;
constexpr int kArmy = 1;
constexpr int kFollower = 2;

void Set(void* entity, int kind, int ownerKey);
void Clear(void* entity);
bool Is(const void* entity);
int Kind(const void* entity);   // kNone / kArmy / kFollower
int Owner(const void* entity);  // owner key, or -1 when none / unmarked

} // namespace FabricatedMark
