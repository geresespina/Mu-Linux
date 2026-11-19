#pragma once

// ActionJump.h — MEAMU Train 24 (2026-11-19) built SPACE = JUMP here;
// MEAMU Train 35 (2026-11-19): DESIGN PIVOT by G — the jump is REMOVED.
// Space in Action mode is now a FACING TELEPORT (see ActionJump.cpp).
//
// What this module owns after the pivot:
//   * The Train 28 XY smoothing spring (fluid WASD) — the render seam
//     this file was already hosting. Unchanged.
//   * The Space edge in Action mode -> facing blink trigger.
//   * The Train 29 air API (Airborne / OnAirDash / CancelAir) as
//     documented no-ops: with no hop there is no air state, so
//     air-dash lost its entry (dash chains live on the ground; the
//     Space blink is the chain's new extension — see Train 35 report).
//
// Classic is untouched: its Space attack latch never engages here.

class CActionJump
{
public:
    // Per-step update, called beside gActionCamera.Update() from
    // MoveMainCamera (Render_WorldHelpers.cpp). Owns the hero's
    // render XY offset (Train 28 spring, restore-first, render-only)
    // and the Action-mode Space edge.
    void Update();

    // Train 35: no air state exists anymore — always grounded.
    bool Airborne() const { return false; }

    // Train 29 API, kept as no-ops so the dash/teleport call sites in
    // ActionMove stand unchanged. There is no air to dash in or cancel.
    void OnAirDash();
    void CancelAir();

private:
    bool     m_SpacePrev = false;
    unsigned m_LastTick = 0;

    // MEAMU Train 28: XY smoothing spring (fluid WASD). The rendered
    // position chases the logical one through a damped spring — see
    // ActionJump.cpp. Action scheme only; Classic renders stock.
    bool     m_SpringInit = false;
    float    m_SmoothX = 0.0f, m_SmoothY = 0.0f;  // chased render position
    float    m_AppliedX = 0.0f, m_AppliedY = 0.0f; // XY offset currently added
};

extern CActionJump gActionJump;
