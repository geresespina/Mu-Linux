#pragma once
#include "Config/UserSettings.h"  // ControlScheme (shared accessor below)

// ActionMove.h — MEAMU Train 13 (2026-11-19): the ACTION control scheme.
//
// Control is a selectable SCHEME (Config.ini [Controls] Scheme, persisted
// by CUserSettings; Options > General selector; the GRAVE/backtick key
// flips it in-world — census in ActionMove.cpp: grave had no consumer,
// and the SDL mapper did not even route it until Train 13):
//
//   CLASSIC (0, DEFAULT) — click-to-move exactly as the client has always
//     played. A Config.ini without the key never sees Train 13. WASD does
//     nothing, the Space latch works, clicks work.
//   ACTION (1) — third-person scheme:
//     * WASD + arrow keys steer the hero directly, camera-relative
//       (computed from the live camera every tick via world->screen
//       projection, so a rotated camera rotates the mapping with it).
//       Hold = keep walking, release = stop. 8-way with diagonals.
//     * The mouse is AIM, not locomotion: the hero's casts and basic
//       attacks resolve against the stock hover target (SelectedCharacter,
//       maintained by Mouse_UpdateHoverTargets from the real cursor), and
//       a physical left-click on the world neither moves nor attacks
//       (Player_InputTick suppresses the physical button in ACTION).
//       Click-to-move is therefore off; the mouse still drives every UI
//       panel exactly as in CLASSIC.
//     * Double-tapping a direction (WASD or arrows, second press within
//       300 ms of the first tap's release) DASHES that way: a request to
//       the server (Bounce 13 wire), never a local teleport. The second
//       tap's own walk step is suppressed — the dash replaces it.
//
// Movement rides the same rails as NavMove/KbPlay: a virtual cursor parked
// ahead of the hero + a held virtual left button, synthesized through
// Platform::DeliverSDLInput, so the game's own hold-to-move pathfinding
// issues the same steps at the same cadence a click would (the GS paces
// on MoveSpeed; there is no packet spam and no speed gain vs clicking).
//
// ActionMove_Tick() runs once per frame from the client main loop, right
// after KbPlay_Tick() (Platform/ClientEntry.cpp), so this module's park
// is the last word each frame when it drives.
//
// BLINK-ENGAGE (both schemes, engagement law — not a WASD feature):
// ActionMove_TryBlinkEngage() is the ONE shared hook the engagement
// drives call when their target is out of reach: Player_InputTick's
// hover-attack branch (mouse click-attack AND the Space latch, which
// rides that same branch on its virtual button) and KbPlay's AimAndPark
// (digit holds + F8 AUTO aim — AUTO's gap-closer). It asks the server to
// land the hero beside the target instead of walking over, at most ONE
// attempt per engagement (guard resets when range closes or the target
// changes); callers keep their normal walk-in in the same frame, so a
// refused or unanswered blink degrades to the exact stock walk-in.
//
// WIRE (LIVE, Train 15 integration): the Bounce 13 dash packet is final
// (builds/bounce13-dash-2026-11-19/DASH-WIRE.md) — one C1 frame, 7 bytes:
// [C1][07][F3][FD][mode][b5][b6]; mode 0 = direction dash (b5 = engine
// Dir 0..7, b6 = 0), mode 1 = dash-to-target (b5/b6 = target server
// index HB/LB). Sends live in SendDashRequest() in ActionMove.cpp.

void ActionMove_Tick();

 // True while the ACTION scheme is live in-world. Read by
// Player_InputTick (physical-click suppression) and KbPlay (Space latch
// gate). CLASSIC always reads false.
bool ActionMove_IsAction();

// THE shared scheme accessor (Train 14's pointer-lock camera keys off
// this same state — wire it here, not to the settings object, so the
// runtime cache and the persisted value can never disagree mid-frame).
ControlScheme ActionMove_Scheme();

// Increments on EVERY scheme flip (hotkey or module-driven). Train 14
// edge-detects flips by sampling this serial, the RealInputSerial
// pattern: sample, compare, react — no callback registration needed.
unsigned ActionMove_SchemeFlipSerial();

// Blink-engage hook (see header note). targetIdx = CharactersClient
// index; attackRangeTiles = the caller's reach for that target (1 for a
// basic attack, the skill's distance for a cast). Returns true only on
// the one frame per engagement that actually sends the request.
bool ActionMove_TryBlinkEngage(int targetIdx, int attackRangeTiles);
