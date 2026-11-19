#pragma once

// ActionJump.h — MEAMU Train 24 (2026-11-19): SPACE = JUMP in Action mode.
//
// The GunZ wave's foundation verb (G's order, 2026-11-19). The engine has
// no jump state and its movement re-pins the hero's height to the terrain
// every step, so the hop is a RENDER-SEAM arc: this module raises the
// hero's draw height above the terrain on a parabolic arc while the
// logical position — pathing, picking, server sync — never changes.
// WASD momentum carries through the air untouched; the dash composes.
//
// What it owns for real is the AIR-STATE (grounded / airborne / landing),
// exposed via Airborne() — air-dash and wall-run hook it in later trains.
//
// Scope v1 (feel first): Action scheme + pointer-locked only. Classic is
// untouched (its Space latch never engages in Action mode anyway). The
// hop is local to this client — other players see the hero grounded.
// No packets, no server change.

class CActionJump
{
public:
    // Per-step update, called beside gActionCamera.Update() from
    // MoveMainCamera (Render_WorldHelpers.cpp).
    void Update();

    bool Airborne() const { return m_Phase != 0; }
    int  Phase() const { return m_Phase; }  // 0 grounded · 1 airborne · 2 landing

private:
    int      m_Phase = 0;
    float    m_T = 0.0f;        // seconds into the current phase
    float    m_Applied = 0.0f;  // Z offset currently added to the hero
    bool     m_SpacePrev = false;
    unsigned m_LastTick = 0;
};

extern CActionJump gActionJump;
