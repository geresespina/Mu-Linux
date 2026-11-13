#pragma once
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

// True while the pad is actively steering/clicking this frame (left stick
// deflected or a trigger held). NavMove (follow / arrow keys) yields to it.
bool Gamepad_IsDriving();

// Draws the small "PAD" indicator (HUD render phase only).
void Gamepad_RenderIndicator();
