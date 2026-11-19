#pragma once

// MEAMU TRAIN 11 -- EXACT STATS presentations (Bounce 12 / F3:FB).
//
// Two views of the ONE server-fed snapshot in Net/ExactStats.h:
//
//   SLIM LINE (always on): a single compact row directly above the
//   Boost Strip -- DMG range - ATK SPD - DEF - FORM pool (when a
//   form is up) - drain %/s (when > 0). HP stays on the stock
//   orbs/numbers, untouched. No icons, no new screen furniture;
//   it reads in the Boost Strip's own chip language so the two
//   rows feel like one instrument.
//
//   FULL SHEET (toggle): every packet field, plainly labelled.
//   Toggle = F6 (free by census: no consumer of VK_F6 exists in
//   this client; 'C' is the stock character window and is NOT
//   touched) or a click on the slim line.
//
// Both draw nothing until the first F3:FB snapshot arrives, so an
// older GS shows neither (the Rena HUD rule).
namespace ExactStatsHud {

// Called from RenderInformation beside gBoostStrip.Render().
void Render();

} // namespace ExactStatsHud
