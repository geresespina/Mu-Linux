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
//   Space     attack latch first (MEAMU Train 8 addendum, G 2026-11-19;
//             Train 9, same day: standalone + widened): a tap locks the
//             closest attackable monster ON SCREEN (the whole visible
//             viewport, no range cap; walk-in closes the distance) and
//             the hero basic-attacks it continuously with no key held;
//             the latch releases the moment the target dies/despawns,
//             and a second tap cancels it. STANDALONE: it works with
//             the F9 keyboard master switch ON or OFF (Train 8 gated
//             it on the switch and mouse-first players got a dead
//             key). Keyboard-only exception to the pad no-latch law,
//             ordered by G; the gamepad and F8/F9 are untouched. Only
//             when no attackable target is on screen (or the world
//             gates are closed) does Space keep its stock HUD pickup
//             job (true-nearest pick with the switch on, stock pick
//             with it off).
//   F8        AUTO mode: keep casting AUTO's skill until toggled off,
//             a modal opens, the scene changes or the hero dies. Chat
//             does NOT stop it — while the chat input is open every
//             key (F8 included) belongs to the chat. The mouse does
//             NOT stop it either (Train 12 cursor freedom): the cursor
//             stays the player's while AUTO runs — the drive parks
//             only on a target change/drift or after the cursor sits
//             still, never while the player moves the mouse or holds
//             a button, and click-to-move simply relocates the fight.
//   Cast streak  (Train 12) casting the SAME offensive skill
//             continuously for 5.0 s latches AUTO on with that skill —
//             same engage path as F8. "Continuously" allows gaps up to
//             ~1.5x the skill's own cycle (measured live from the
//             cast intervals, clamped 0.8–3.0 s). Digit holds and
//             clicks count: the tick polls the hero's SkillStage
//             cast-commit stamp. Casts made while the physical right
//             mouse button is held NEVER count toward the span (RMB
//             law, 2026-11-16, G's order: RMB must never latch AUTO),
//             and neither do gamepad skill-button casts (pad law,
//             same date) — a pad hold can never arm AUTO. Self/buff/
//             mode skills never count at all. After AUTO turns off, a
//             fresh span cannot arm until casting has fully stopped
//             for ~0.8 s (anti-re-latch beat).
//   AUTO steering  (Train 12) while AUTO runs, manual casts steer it:
//             recasting AUTO's own skill by hand turns AUTO OFF (the
//             cast still fires); casting a different offensive skill
//             switches AUTO to it. AUTO's own casts do neither — they
//             carry no manual source flag (no digit hold, no pad
//             button, no physical mouse button).
//   F9        keyboard-controls master toggle (MEAMU kb-toggle,
//             2026-11-13): flips the keyboard feature ON/OFF in-world
//             and persists Config.ini [Keyboard] Enable. OFF silences
//             every KbPlay KEYBOARD input path at once (digits, Tab
//             cycle, Enter, the Space nearest-pick upgrade). The Space
//             attack latch is decoupled from the switch (Train 9):
//             it engages and runs with the switch on or off. AUTO is
//             decoupled from the switch (2026-11-15): F8 toggles it
//             with the switch on or off, an F9 flip mid-AUTO leaves
//             AUTO running, and the cast-streak latch arms either way.
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

// Space hook called by HUD_HotkeyTick on the Space press edge (same
// pattern as KbPlay_TabCycle): true when the tap latched a target or
// cancelled the latch (Space consumed), false when Space keeps its
// stock pickup job. NOT gated on the keyboard master switch (Train 9).
bool KbPlay_SpaceTap();

// The keyboard-controls master state (boot config + F9 flips). Read by the
// Space pickup handler so OFF restores the stock first-in-pool pick.
bool KbPlay_Enabled();

// Draws the small "AUTO" indicator while AUTO mode runs (HUD render phase
// only; called from RenderInformation next to Gamepad_RenderIndicator).
void KbPlay_RenderAutoIndicator();
