#pragma once

// MEAMU Train 82: the teleport-targeting verbs (Train 59) are
// shared -- the pad drives them from LT, the keyboard from
// Space (Action-mode parity). One marker state (Gamepad.cpp's
// gTpT) serves both. kbdAim=true: the marker rides the camera
// aim instead of the right stick.
void TpTargetBegin();
bool TpTargetUpdate(float dt, bool kbdAim);
void TpTargetFire();
void TpTargetCancel(const char* why);
bool TpTargetActive();

// MEAMU Train 84: Gamepad's queued key pulse (anonymous-
// namespace inside Gamepad.cpp), wrapped so the keyboard's Q
// can fire LB's exact Classic Tab pulse.
void PulseKeyShared(int vk, const char* what);
// Gamepad.h — MEAMU rev.5 controller support (full build).
//
// Gamepad_Tick() runs once per frame from the client main loop, right after
// ServiceEvents(), so synthesized pad input lands in the same frame's input
// processing in every scene (title, char-select, in-world). The pad never
// calls game logic directly: it synthesizes the exact mouse/keyboard input
// the game already consumes (Platform::DeliverSDLInput events + virtual key
// state), per builds/controller-lab-2026-11-12/MAPPING.md.
//
// Config.ini [Gamepad]: Enable (1/0), Deadzone (percent, default 25),
// CursorSpeed (px/s, default 900) — see Config/UserSettings.

void Gamepad_Tick();

// True while a gamepad is connected and the module is enabled. Drives the
// on-screen indicator (Gamepad_RenderIndicator, called from the HUD render).
bool Gamepad_IsActive();

// True while the pad is steering with the left stick this frame (direct
// move active). NavMove (follow / arrow keys) yields to it, and so does
// KbPlay's held cast. Skill buttons never set it: they are digit mirrors
// (PADCAST, 2026-11-16), and like a keyboard digit hold they must not
// make a cast yield.
bool Gamepad_IsDriving();

// True while any of the ten pad skill buttons (A/X/Y/B/RB/LB/RT/LT/L3/R3)
// is held. KbPlay's cast-streak latch reads it: a cast observed while a
// pad skill button drives it is excluded from the AUTO latch (pad law),
// exactly like a cast made while the physical right mouse button is held.
bool Gamepad_PadCastHeld();

// Draws the small "PAD" indicator (HUD render phase only).
void Gamepad_RenderIndicator();
