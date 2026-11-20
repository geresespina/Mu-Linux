#pragma once
// MoveMarkers.h — MEAMU click-to-move + teleport markers (client only,
// G ask 2026-11-18 02:36: "can we have a visual marker when i click on
// the ground to move? or when I teleport?").
//
// Two markers, both drawn by Render_MoveMarkers in the HUD pass (the
// BloodGore slot in Render_GameFrame) as projected geometry — no new
// art assets, no bitmap slots:
//
//   1. CLICK MARKER — when the HERO gets a ground-move order from a
//      real MOUSE CLICK, a gold-white expanding ring + descending
//      chevron flashes on the destination tile (~0.55 s). One marker
//      per click; a new click replaces it. Mouse-only: the caller
//      (the pure ground-click success in Player_InputTick) passes the
//      gate facts and this module applies them — the one-shot
//      MouseLButtonPush edge, Platform::RealInputSerial advanced
//      (synthesized input from the pad / KbPlay / NavMove drivers
//      never advances it), the physical left button held in the SDL
//      watch's own key table (also real-only), and no virtual driver
//      steering. Hold-to-move re-paths carry no edge and never fire.
//
//   2. TELEPORT MARKERS — any character in view (hero included)
//      arriving by teleport gets a cyan-white expanding ring + light
//      pillar + spark swirl at the landing tile (~0.8 s); departures
//      get a smaller shrinking ring where the clean departure path
//      exists (CreateTeleportBegin — Teleport skill / Teleport Party).
//      Arrivals hook CreateTeleportEnd, the single funnel every
//      teleport arrival already flows through (NetRecv_1C skill
//      teleports, viewport spawn state, Teleport Party), plus
//      NetRecv_1C's gate branch (map moves deliberately skip
//      CreateTeleportEnd, so the hook sits at the handler tail).
//
// Toggle: Config.ini [Game] MoveMarker 1/0 (default 1) via
// gUserSettings.GetMoveMarker(). Off = zero behaviour change: every
// entry point early-outs and the render pass draws nothing.

namespace MoveMarkers {

// Hero ground-click destination. Called ONLY from the ground-click
// success in Player_InputTick (Path_FindRoute ok, move packet about
// to be sent) — never from the mob/NPC/item branches, which return
// earlier in that function. gridX/gridY are the clicked tile.
void OnGroundClick(int gridX, int gridY, bool clickEdge,
                   unsigned realSerial, bool virtualDriving,
                   bool physicalDown);

// Any character arriving by teleport. entity = the CHARACTER/OBJECT
// pointer (position read from +0x10/+0x14/+0x18, as the receive code
// does). Safe with null.
void OnTeleportArrival(const void* entity);

// Any character leaving by teleport (CreateTeleportBegin path only).
void OnTeleportDeparture(const void* entity);

// MEAMU Train 63 — TELEPORT VANISH (hero only, G's ruling: "I don't
// want the body pose but I want the disappearing player when
// teleporting"). Pure render dressing, gate=0 BY CONSTRUCTION:
// the envelope is a function of time read ONLY inside
// RenderCharacter's draw (via an RAII scope that restores
// OBJECT::Alpha before the function returns), so no sim or input
// code ever observes a faded hero — the input tick's Alpha<0.7
// click gate (the Train 43 click-eater) can never trip. No pose,
// no Teleport state, no AlphaTarget machine.
//   KickSend    — a teleport-class dash frame (modes 1/2) left:
//                 fade-out starts + departure dressing fires.
//   KickArrival — the teleport/move result landed: fade-in starts.
//   Envelope    — current alpha multiplier for the hero's draw.
void TeleportVanishKickSend(const void* heroEntity);
void TeleportVanishKickArrival();
float TeleportVanishEnvelope();

// Test/diagnostic surface (also used by the env-gated self-test).
int  ActiveClickMarker();
int  ActiveTeleportMarkers();
void ClearAll();

} // namespace MoveMarkers

// HUD-pass render: rings, chevron, pillars. Declared free (not in the
// namespace) to match Render_BloodGore / Render_HpBars.
void Render_MoveMarkers(void);
