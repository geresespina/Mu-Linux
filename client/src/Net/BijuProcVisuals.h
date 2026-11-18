#pragma once
// BijuProcVisuals.h -- MEAMU Biju Powers proc visuals (client half,
// Train 2 Module B, G approved 2026-11-18). See BijuProcVisuals.cpp
// for the design. Wire: custom packet C1 F3 F9 from the GS module
// CustomBijuPowers (F3 subcode audit: client NetRecv_F3 uses
// F0/F1/F2/F5/F6, F7 = HP bars, F8 = Boost Strip; F9 was free on both
// sides). An old GS never sends F9, so this module is inert until the
// paired GS ships -- no toggle needed.

#include <cstdint>

namespace BijuProcVisuals {

// Particle texture: bitmap slot 0x100 (256) -- audit in
// BijuProcVisuals.cpp. Particle type rides the same slot (the
// BloodGore 259 pattern).
constexpr int kBitmap = 256;              // 0x100

// F3/F9 receive handler (wired in NetRecv_F3). Validates and drops
// malformed packets.
void Receive(const uint8_t* msg, int size);

// Test/diagnostic surface (also used by the env-gated self-test).
int BearerStateCount();
int ActiveParticles();
void ClearAll();

} // namespace BijuProcVisuals

// HUD-pass render: orbiting Truth-Seeker Orbs, Steam Pressure puffs
// and ignite/acid embers, driven by the bearer-state table + timed
// overlay list. Declared free to match Render_BloodGore.
void Render_BijuProcVisuals(void);
