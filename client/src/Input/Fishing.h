#pragma once
// MEAMU Train 56 (2026-11-19) -- FISHING client half (server: Bounce 24).
// One verb, three doors: F key (both schemes), pad D-pad Left (Action
// scheme), and the server owns everything else. Wire: C1 F3 FE.
//   C->S [C1 05 F3 FE 00]        cast / reel
//   S->C [C1 07 F3 FE 01 bx by]  cast accepted -- bobber tile
//   S->C [C1 05 F3 FE 02]        BITE -- splash + sound, reel now
void Fishing_Verb();
void Fishing_Receive(const unsigned char* Msg, int Size);
