#pragma once
// NavMove.h — MEAMU rev.5 keyboard navigation: arrow-key movement + the
// client-side /follow command.
//
// NavMove_Tick() runs once per frame from the client main loop, right after
// Gamepad_Tick(). Movement is synthesized through the same virtual-cursor +
// virtual-left-button mechanism the gamepad uses (park the cursor ahead of
// the hero, hold the button, the game's own hold-to-move pathfinding walks).

void NavMove_Tick();

// True while any arrow key is held (and the nav gates pass). The gamepad
// module yields movement ownership to the arrows while this is true.
bool NavMove_ArrowsHeld();

// Chat hook for Chat_SendChatLine (UI/Chat_InputTick.cpp): returns true when
// `text` is a /follow command (consumed locally, never sent to the server).
//   /follow <name>  — start following that player
//   /follow         — stop following
bool Follow_TryCommand(const char* text);

// True while a follow target is engaged.
bool Follow_IsActive();
