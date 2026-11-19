// ActionJump.cpp — MEAMU Train 24 (2026-11-19): SPACE = JUMP (Action mode).
// See ActionJump.h for the design. Feel numbers are G's to verdict —
// they are one-line tunables below.

#include "stdafx.h"
#include "Input/ActionJump.h"

#include "Platform/IInput.h"      // Platform::GetInput (physical key state)
#include "Entity/EntityTypes.h"   // CHARACTER
#include "Render/ActionCamera.h"  // gActionCamera (lock + scheme state)
#include "globals.h"              // Hero, InputEnable, DAT_083a7c24
#include <SDL3/SDL.h>             // SDL_GetTicks
#include <cmath>

CActionJump gActionJump;

namespace {
    // ── Feel tunables (G verdicts these on the real build) ──────────────
    const float kJumpPeak    = 60.0f;   // world units (100 = 1 tile) ≈ waist height
    const float kJumpSeconds = 0.60f;   // full up-and-down arc
    const float kLandSeconds = 0.08f;   // landing squash duration
    const float kLandDip     = 6.0f;    // landing squash depth (units)

    CHARACTER* HeroC() { return (CHARACTER*)(uintptr_t)Hero; }

    bool SpaceHeld()
    {
        return ((unsigned short)Platform::GetInput().KeyState(VK_SPACE) & 0x8000) != 0;
    }
}

void CActionJump::Update()
{
    CHARACTER* hero = HeroC();

    // Restore-first: whatever offset last step applied comes off before
    // anything else reads the hero's height this step. Game logic only
    // ever sees the true terrain height; the offset exists purely
    // between this call and the frame's character draw.
    if (m_Applied != 0.0f) {
        if (hero) hero->Object.Position[2] -= m_Applied;
        m_Applied = 0.0f;
    }
    if (!hero) { m_Phase = 0; m_T = 0.0f; m_SpacePrev = false; m_LastTick = 0; return; }

    const unsigned now = (unsigned)SDL_GetTicks();
    float dt = m_LastTick ? (float)(now - m_LastTick) / 1000.0f : 0.0f;
    m_LastTick = now;
    if (dt > 0.1f) dt = 0.1f;  // clamp tab-outs / hitches

    const bool space = SpaceHeld();
    const bool spaceEdge = space && !m_SpacePrev;
    m_SpacePrev = space;

    // Start gate: Action scheme engaged (pointer-locked), no text/UI
    // focus (same pair the WASD drive gates on), currently grounded.
    // Once airborne the arc always finishes — losing the lock mid-hop
    // never freezes him in the air.
    if (m_Phase == 0 && spaceEdge && gActionCamera.PointerLocked()
        && InputEnable == 0 && DAT_083a7c24 == 0) {
        m_Phase = 1;
        m_T = 0.0f;
    }

    float z = 0.0f;
    if (m_Phase == 1) {
        m_T += dt;
        const float p = m_T / kJumpSeconds;
        if (p >= 1.0f) { m_Phase = 2; m_T = 0.0f; }
        else z = kJumpPeak * 4.0f * p * (1.0f - p);  // parabola: soft top, weighty ends
    } else if (m_Phase == 2) {
        m_T += dt;
        const float p = m_T / kLandSeconds;
        if (p >= 1.0f) { m_Phase = 0; m_T = 0.0f; }
        else z = -kLandDip * sinf(3.14159265f * p);  // squash and recover
    }

    if (z != 0.0f) {
        hero->Object.Position[2] += z;
        m_Applied = z;
    }
}
