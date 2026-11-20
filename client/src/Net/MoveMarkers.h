#pragma once
// MoveMarkers.h — MEAMU click-to-move marker (client only,
// G ask 2026-11-18 02:36: "can we have a visual marker when i click
// on the ground to move?").
//
// CLICK MARKER — when the HERO gets a ground-move order from a
// real MOUSE CLICK, a gold-white expanding ring + descending
// chevron flashes on the destination tile (~0.55 s). One marker
// per click; a new click replaces it. Mouse-only: the caller
// (the pure ground-click success in Player_InputTick) passes the
// gate facts and this module applies them — the one-shot
// MouseLButtonPush edge, Platform::RealInputSerial advanced
// (synthesized input from the pad / KbPlay / NavMove drivers
// never advances it), the physical left button held in the SDL
// watch's own key table (also real-only), and no virtual driver
// steering. Hold-to-move re-paths carry no edge and never fire.
//
// MEAMU Train 77 (2026-11-20, G's ruling): the TELEPORT markers
// (arrival ring + light pillar + spark swirl, departure ring)
// and the Train 63/69 teleport VANISH are REMOVED — G saw the
// pillar as a bright triangle/cone over the cast and ordered
// the original teleport cast animations back. Teleports now
// render exactly stock: CreateTeleportBegin/End (action 87,
// AlphaTarget fade, sparkle 1176, sound 88) with no hooks, and
// the gate-0 arrival read keeps its stock 1176 + 88 only. The
// server landing timing (Bounces 28/29) is untouched.
//
// Toggle: Config.ini [Game] MoveMarker 1/0 (default 1) via
// gUserSettings.GetMoveMarker(). Off = the click marker draws
// nothing.

namespace MoveMarkers {

// Hero ground-click destination. Called ONLY from the ground-click
// success in Player_InputTick (Path_FindRoute ok, move packet about
// to be sent) — never from the mob/NPC/item branches, which return
// earlier in that function. gridX/gridY are the clicked tile.
void OnGroundClick(int gridX, int gridY, bool clickEdge,
                   unsigned realSerial, bool virtualDriving,
                   bool physicalDown);

// Test/diagnostic surface (also used by the env-gated self-test).
int  ActiveClickMarker();
void ClearAll();

} // namespace MoveMarkers

// HUD-pass render: the click ring + chevron. Declared free (not in
// the namespace) to match Render_BloodGore / Render_HpBars.
void Render_MoveMarkers(void);
