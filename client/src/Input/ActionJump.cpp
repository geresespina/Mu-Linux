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
    const float kGlideHold   = 0.12f;   // Train 29: height held after an air-dash
    const float kGlideFall   = 0.30f;   // Train 29: then a soft quadratic descent

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
    if (m_AppliedX != 0.0f || m_AppliedY != 0.0f) {
        if (hero) {
            hero->Object.Position[0] -= m_AppliedX;
            hero->Object.Position[1] -= m_AppliedY;
        }
        m_AppliedX = m_AppliedY = 0.0f;
    }
    if (!hero) { m_Phase = 0; m_T = 0.0f; m_SpacePrev = false; m_LastTick = 0; m_SpringInit = false; return; }

    const unsigned now = (unsigned)SDL_GetTicks();
    float dt = m_LastTick ? (float)(now - m_LastTick) / 1000.0f : 0.0f;
    m_LastTick = now;
    if (dt > 0.1f) dt = 0.1f;  // clamp tab-outs / hitches

    // ── MEAMU Train 28: XY smoothing spring (Action scheme only) ──────
    // The engine walks tile waypoints at one fixed speed: velocity
    // snaps at every corner and off-axis diagonals zigzag. The hero's
    // RENDERED position chases the logical one through a ~70 ms damped
    // spring: corners round off, zigzags blend into a curve, starts
    // ease in and stops settle out. The logical position — server,
    // targeting, pathing — is never touched (restore-first, above),
    // the chase is capped at a quarter tile so the render never
    // detaches from the action, and a >1.5-tile jump (teleport, zone
    // change) snaps instead of gliding.
    float offX = 0.0f, offY = 0.0f;
    if (!gActionCamera.SchemeIsAction()) {
        m_SpringInit = false;   // Classic renders the stock position
    } else {
        const float lx = hero->Object.Position[0];
        const float ly = hero->Object.Position[1];
        if (!m_SpringInit) {
            m_SmoothX = lx; m_SmoothY = ly; m_SpringInit = true;
        } else {
            const float dx = lx - m_SmoothX, dy = ly - m_SmoothY;
            if (dx * dx + dy * dy > 150.0f * 150.0f) {
                m_SmoothX = lx; m_SmoothY = ly;   // snap, never glide
            } else if (dt > 0.0f) {
                const float a = 1.0f - expf(-dt / 0.07f);
                m_SmoothX += dx * a;
                m_SmoothY += dy * a;
            }
            offX = m_SmoothX - lx; offY = m_SmoothY - ly;
            const float len = sqrtf(offX * offX + offY * offY);
            if (len > 25.0f) {   // cap the chase AT the spring state
                const float s = 25.0f / len;
                m_SmoothX = lx + offX * s;
                m_SmoothY = ly + offY * s;
                offX *= s; offY *= s;
            }
        }
    }

    const bool space = SpaceHeld();
    const bool spaceEdge = space && !m_SpacePrev;
    m_SpacePrev = space;

    // Start gate: Action scheme, no text/UI focus (same pair the WASD
    // drive gates on), currently grounded. Once airborne the arc
    // always finishes.
    // MEAMU Train 30: the gate no longer requires PointerLocked. On
    // G's Mac (1520) Space never fired — every other condition checks
    // out in code (Space scancode -> VK 0x20 is mapped, WASD proves
    // the InputEnable/modal pair, this module IS called beside the
    // camera), leaving the SDL relative-mode lock state as the one
    // gate that can silently read false while Action play otherwise
    // works. The lock is a camera concern, not a jump condition.
    if (m_Phase == 0 && spaceEdge && gActionCamera.SchemeIsAction()
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
    } else if (m_Phase == 3) {
        // Train 29 air-dash glide: hold the height a breath (the dash
        // burst), then descend quadratically into the landing squash.
        m_T += dt;
        if (m_T < kGlideHold) {
            z = m_GlideFrom;
        } else {
            const float q = (m_T - kGlideHold) / kGlideFall;
            if (q >= 1.0f) { m_Phase = 2; m_T = 0.0f; }
            else { const float r = 1.0f - q; z = m_GlideFrom * r * r; }
        }
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
    if (offX != 0.0f || offY != 0.0f) {
        hero->Object.Position[0] += offX;
        hero->Object.Position[1] += offY;
        m_AppliedX = offX; m_AppliedY = offY;
    }
}

// MEAMU Train 29: a direction dash fired mid-air. The arc converts to a
// glide from the CURRENT height (m_Applied is the offset in force —
// the height he is visibly at); chaining another dash refreshes the
// glide from wherever he is then. Grounded / landing: nothing to do.
void CActionJump::OnAirDash()
{
    if (m_Phase != 1 && m_Phase != 3) return;
    m_GlideFrom = m_Applied > 0.0f ? m_Applied : 0.0f;
    if (m_GlideFrom <= 0.0f) { m_Phase = 0; m_T = 0.0f; return; }
    m_Phase = 3;
    m_T = 0.0f;
}

// MEAMU Train 29: a teleport-class move ends the air state NOW — the
// offset comes off immediately so the arrival handler's terrain snap
// never meets a stale hop offset (no one-step dip).
void CActionJump::CancelAir()
{
    if (m_Phase == 0) return;
    m_Phase = 0;
    m_T = 0.0f;
    CHARACTER* hero = HeroC();
    if (hero && m_Applied != 0.0f) {
        hero->Object.Position[2] -= m_Applied;
        m_Applied = 0.0f;
    }
}
