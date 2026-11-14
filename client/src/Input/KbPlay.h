#pragma once
// KbPlay.h — MEAMU keyboard-only play (2026-11-13, G's request: "play using
// just the keyboard").
//
// KbPlay_Tick() runs once per frame from the client main loop, right after
// NavMove_Tick(). It layers on the rev.5 machinery: casts are a virtual
// right-button hold with the virtual cursor parked on the aim target — the
// exact mechanism the gamepad's LT hold-cast uses — so every stock rule
// (mana, delay, range walk-in, safe zones) applies unchanged.
//
//   1..9, 0   select the quick-slot skill AND cast it (hold = repeat-cast)
//   Tab       cycle/lock a combat target (hooked from HUD_HotkeyTick)
//   Enter     talk to the nearest NPC (virtual click; hero walks up)
//   Space     pickup stays the stock HUD handler (upgraded to true-nearest)
//   F8        AUTO mode: keep casting the selected skill until toggled
//             off, the mouse is grabbed, a modal opens, the scene changes
//             or the hero dies. Chat does NOT stop it — while the chat
//             input is open every key (F8 included) belongs to the chat.
//   Cast streak  casting the selected skill continuously for 2.0 s
//             (no gap over ~0.6 s between casts) latches AUTO on —
//             same engage path as F8. Any input source counts (mouse
//             hold, digit hold, gamepad, clicks): the tick polls the
//             hero's SkillStage cast-commit stamp. After AUTO turns
//             off, a fresh streak cannot arm until casting has fully
//             stopped for ~0.8 s (anti-re-latch beat).
//   F9        keyboard-controls master toggle (MEAMU kb-toggle,
//             2026-11-13): flips the whole feature ON/OFF in-world and
//             persists Config.ini [Keyboard] Enable. OFF silences every
//             KbPlay input path at once (digits, Tab cycle, Enter, the
//             Space nearest-pick upgrade) and takes AUTO down with it;
//             F8 while OFF only announces "KEYBOARD CONTROLS: OFF".
//             Boot state comes from Config.ini ([Keyboard] Enable,
//             default 1 = ON when the section/key is missing). The
//             gamepad is never affected.
//
// KbPlay_TabCycle() is called by HUD_HotkeyTick (Game/Player_InputTick.cpp)
// on the Tab press edge: returns true when it cycled a target (Tab consumed,
// minimap untouched), false when no attackable monster is in range (Tab
// keeps its stock minimap toggle).

void KbPlay_Tick();
bool KbPlay_TabCycle();

// The keyboard-controls master state (boot config + F9 flips). Read by the
// Space pickup handler so OFF restores the stock first-in-pool pick.
bool KbPlay_Enabled();

// Draws the small "AUTO" indicator while AUTO mode runs (HUD render phase
// only; called from RenderInformation next to Gamepad_RenderIndicator).
void KbPlay_RenderAutoIndicator();
