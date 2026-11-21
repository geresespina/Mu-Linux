#pragma once

// =====================================================================
// CONTROLLER MAPPER (MEAMU Train 100, 2026-11-21) -- the pad's
// source of truth moves from hard-coded bindings to the player's
// own account map, saved on the MEAMU site (/controller) and
// pulled at login. The contract (control names, action names,
// the Default preset) is shared verbatim with the site's
// lib/controller-map.js -- change one side, change both.
//
// Fallback law (G's build order): no saved map, fetch failure,
// or no fetch yet -> the DEFAULT preset below, which is tonight's
// final layout exactly. The pad is never broken.
// =====================================================================

enum PadControl {
    PC_LT = 0, PC_LB, PC_L3, PC_DUP, PC_DLEFT, PC_DRIGHT, PC_DDOWN,
    PC_BACK, PC_START,
    PC_RT, PC_RB, PC_Y, PC_X, PC_B, PC_A, PC_R3,
    PC_COUNT
};

enum PadAction {
    PA_NONE = 0,
    PA_DASH,
    PA_TELEPORT,
    PA_LOCK_TOGGLE,
    PA_LOCK_POUR,
    PA_SKILL1, PA_SKILL2, PA_SKILL3, PA_SKILL4, PA_SKILL5,
    PA_SKILL6, PA_SKILL7, PA_SKILL8, PA_SKILL9, PA_SKILL10,
    PA_PICKUP,
    PA_FISHING,
    PA_SCHEME_TOGGLE,
    PA_COUNT
};

// The action bound to a control in the ACTIVE map (the account
// map once one has landed this session, else the Default).
PadAction PadMap_ActionFor(int control);

// The control carrying an action, or -1 (an action lives on at
// most one control -- the site's save path enforces it, and the
// loader here re-enforces it on whatever it parses).
int PadMap_ControlFor(int action);

// True once an account map has been applied this session.
bool PadMap_AccountMapActive();

// Called from CNetwork::SendLogin: kicks a background fetch of
// the account's map (POST /api/controller-map/fetch, game
// credentials). Fire-and-forget; any failure leaves the Default
// in place. One fetch per account per client run.
void PadMap_OnLogin(const char* account, const char* password);
