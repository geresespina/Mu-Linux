#pragma once

#include <cstdint>

// HpBars — MEAMU HP bars on hit (2026-11-17, G: "show HP bars when
// hit, then it disappears again if not hit after a few seconds" —
// ruled for EVERYONE: monsters, Biju, bosses, phantoms, real
// players, and the hero himself).
//
// Wire: the GS damage funnel (CObjectManager::CharacterLifeCheck)
// emits ONE custom packet per damage event — C1 F3 F7, 8 bytes:
// target object index + post-damage HP fraction in per-mille —
// broadcast to the target's whole viewport plus the target itself.
// F3 is this fork's custom family (F6 = MapTrack); F7 was free on
// both sides. Exactness: the fraction is computed server-side from
// Life / (MaxLife + AddLife), so custom-scaled HP (Biju 600k-40M,
// Multiverse bosses 100M, geared players ~167k) reads truthfully —
// no client-side stock table is ever consulted.
//
// Degrade contract: an older client lands F3/F7 in NetRecv_F3's
// default case and ignores it; this client on an older GS simply
// never receives F7 and shows no world bars (the hero's own bar is
// driven locally from gHeroVitals, which needs no wire at all).
//
// This module owns the per-entity records (fraction + last-damage
// tick) and the world render pass. The stock CHealthBar snapshot
// bars (F3/E2) stay untouched; DrawViewport defers to a fresh
// record here so a Biju never wears two bars at once.

namespace HpBars {

// How long a bar stays up after the entity's last damage, and the
// alpha fade-out tail inside that window (the bar "disappears again
// if not hit after a few seconds" — G's words; ~4 s total).
constexpr uint32_t HPBAR_FADE_MS = 4000;
constexpr uint32_t HPBAR_FADE_OUT_MS = 500;

// F3/F7 receive handler (wired in NetRecv_F3). Validates the frame
// and stores the record; malformed packets are dropped.
void Receive(const uint8_t* msg, int size);

// True while `entity` (a CharactersClient slot) has a record inside
// the fade window. CHealthBar::DrawViewport uses it for precedence.
bool HasFresh(const void* entity);

} // namespace HpBars

// World pass: draws every fresh bar over its entity's head. Called
// from Render_GameFrame right after Render_FloatingText, the same
// cadence/neighbourhood as the human name labels.
void Render_HpBars(void);
