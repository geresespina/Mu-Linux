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
//
// TRAIN 17 (movement fix + Feature 3):
// * While the Action Camera is POINTER-LOCKED, the park-and-hold drive
//   cannot work: Input_OnEvent consumes MouseMove as a look delta and
//   the camera pins the cursor globals to screen centre, so the input
//   tick raycast the centre tile for every key and only the forward
//   axis ever stepped (bugs #5/#6). In that state ActionMove drives
//   DIRECTLY (Path_FindRoute + Combat_SendMovePathPacket, the ground-
//   click branch's own tail) and keeps the virtual button up. The
//   park drive above remains the not-locked path, unchanged.
// * Mode 2 on the same wire = CLICK TELEPORT: a physical double-click
//   on a ground point (detected in Player_InputTick's terrain-only
//   ground branch) teleports to that tile — [C1][07][F3][FD][02][x][y],
//   x/y = landing tile. Beyond-range clicks clamp along the
//   hero->click line to the dash MaxRange (8, Chebyshev). Both
//   schemes; server half is Bounce 14.

void ActionMove_Tick();

 // True while the ACTION scheme is live in-world. Read by
// Player_InputTick (physical-click suppression) and KbPlay (Space latch
// gate). CLASSIC always reads false.
bool ActionMove_IsAction();

// MEAMU Train 16 (Fix 4): true while a dash/blink request is in flight
// and movement output must stay killed — from the send frame until the
// teleport lands (hero tile changes), a fresh direction press takes
// control back, or the 600 ms silent-refusal timeout lapses. Read by
// Player_InputTick (walk-in move packet), NavMove (arrow/follow drives)
// and ActionMove's own drive.
bool ActionMove_DashBraking();

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

// Train 48: unconditional blink-to-target for the Space / pad-B verb
// when a target lock is active (no engagement guard — the press IS
// the order). Validates the target, sends the mode-1 dash frame.
bool ActionMove_BlinkToTarget(int targetIdx);

// Train 48: pad dash verb (button A). Sets the pure-dash burst; the
// direction is the currently held drive direction, or the stored
// pad-dash direction (sx, sy in screen space) when no key is held.
void ActionMove_PadDashDir(int sx, int sy);
void ActionMove_PadDash();

// MEAMU Train 17: true while the Action Camera holds the pointer lock.
// NavMove reads it to yield its arrow drive in ACTION+locked, where
// ActionMove samples the arrows itself and direct-drives (its held
// button would otherwise keep routing toward the pinned centre tile).
bool ActionMove_CameraLocked();

// MEAMU Train 17 (Feature 3): double-click ground teleport (dash wire
// mode 2, see header note). Player_InputTick's ground-click branch
// calls this with the clicked tile when it detects a physical
// double-click on terrain; the target is clamped to range here and
// the frame goes out through the dash choke point (Train 16 brake
// discipline included). Returns true only when the frame was sent.
bool ActionMove_TeleportToPoint(int tileX, int tileY);

// MEAMU Train 25: shared double-click pairing (event-path instant fire).
bool ActionMove_TickDoubleClick(int tileX, int tileY, int uiX, int uiY, unsigned serial);
bool ActionMove_TryEventTeleport(int uiX, int uiY);

// MEAMU Train 20: ONE direct route issue toward an arbitrary tile —
// the DirectDriveIssue tail (Path_FindRoute into the hero's path
// buffer, MovementType = 0, Combat_SendMovePathPacket), exported for
// the drives whose steering channel is the parked cursor and which
// therefore cannot steer while the camera is pointer-locked (Train
// 17 root cause): KbPlay's cast/AUTO/latch walk-in and NavMove's
// /follow. Click-parity gate and Train 16 dash-brake discipline are
// inside; the CALLER owns the re-issue cadence and the target choice.
// Returns true only when a move packet actually went out.
bool ActionMove_DirectRouteTo(int tileX, int tileY);
