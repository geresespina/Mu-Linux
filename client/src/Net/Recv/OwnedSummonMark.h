#pragma once
// OwnedSummonMark.h — MEAMU (Train 8 addendum; G's ruling 2026-11-19:
// no name labels floating over summons and follower Biju).
//
// The clutter G screenshotted ("Gloccy's Kurama", "Gloccy's Shukaku", ...)
// is the player-nameplate system (Render/HumanLabels.cpp) doing its job on
// the wrong bodies: the summon viewport handler (NetRecv_1F, packet 0x1F)
// creates every owned summon with Object.Kind == 1 (the player kind — see
// EntityTypes.h: OBJECT::Kind @ +0x84), so the nameplate pass bubbles them
// exactly like players, using the "<owner>'s <beast>" label the same
// handler composes from the packet's structured OwnerName field.
//
// The mark records WHICH pool slots arrived through that summon packet —
// the creation route is the owner flag. It is keyed on the packet, never
// on name text: real players (player viewport packet) are never marked and
// keep their nameplates; wild Biju (monster viewport packet) are never
// marked and keep theirs.
//
//   Set   — NetRecv_1F only, right after the summon is created.
//   Clear — CreateCharacterPointer (the universal pool-slot reset every
//           player / monster / summon (re)creation flows through), so a
//           recycled slot never inherits the mark.
//   Is    — HumanLabels skips marked slots.
//
// The table + functions live in Recv_Viewport.cpp (the 0x1F handler's TU);
// no new translation unit, so no CMake / harness registration is needed.
//
// NOT covered, by design: clan summons. The GS spawns those as fabricated
// USERS (CustomClanSummon, Type=OBJECT_USER), so they arrive through the
// player viewport packet bit-identical to real players — no client-side
// kind/owner flag exists for them. That half is a server item, documented
// in TRAIN8-ADDENDUM.md (GS-side routes: spawn them as gObjAddSummon
// monsters like every other follower system, or add a client-visible flag
// to the player viewport record).

namespace OwnedSummonMark {
void Set(void* entity);
void Clear(void* entity);
bool Is(const void* entity);
} // namespace OwnedSummonMark
