#pragma once
// TRAIN 116 (2026-11-23, G's ask): AUTO-LOGIN -- launch -> in-world, no clicks.
// Driven by a LOCAL file, autologin.ini, sitting beside the client (the same
// convention as Config.ini). The file is NEVER part of the repo (gitignored);
// credentials live only on the player's own machine. Opt-in: no file, or
// Enabled=0, and the client behaves exactly as stock.
//
//   [AutoLogin]
//   Enabled=1
//   Account=<account>
//   Password=<password>
//   Character=<character name>
//
// The driver reuses the stock flow's own seams (the server-select click tail,
// CNetwork::SendLogin, the char-select entry state) -- it invents no packets.

void AutoLogin_Tick();        // per-frame, self-gating (login + char-select scenes)
void AutoLogin_OnJoinServer(); // F1/00 received: send the configured login once
