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
//
// KbPlay_TabCycle() is called by HUD_HotkeyTick (Game/Player_InputTick.cpp)
// on the Tab press edge: returns true when it cycled a target (Tab consumed,
// minimap untouched), false when no attackable monster is in range (Tab
// keeps its stock minimap toggle).

void KbPlay_Tick();
bool KbPlay_TabCycle();

// Draws the small "AUTO" indicator while AUTO mode runs (HUD render phase
// only; called from RenderInformation next to Gamepad_RenderIndicator).
void KbPlay_RenderAutoIndicator();
