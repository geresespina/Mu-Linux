// ActionJump.cpp — MEAMU Train 24 built SPACE = JUMP here; MEAMU
// Train 35 (2026-11-19), DESIGN PIVOT by G: the jump is REMOVED and
// Space in Action mode is a FACING TELEPORT — an instant blink to the
// furthest reachable point along his facing (the Bounce 14 mode-2
// furthest-point logic, direction = facing instead of a click).
// What remains in this module: the Train 28 smoothing spring and the
// Space edge that fires the blink. See ActionJump.h.

#include "stdafx.h"
#include "Input/ActionJump.h"

#include "Platform/IInput.h"      // Platform::GetInput (physical key state)
#include "Entity/EntityTypes.h"   // CHARACTER
#include "Render/ActionCamera.h"  // gActionCamera (lock + scheme state)
#include "Input/ActionMove.h"
#include "Input/AnimTrace.h"
#include "Input/TargetLock.h"   // Train 48
#include "Input/Fishing.h"      // Train 56: fishing verb   // MEAMU Train 39 diagnostic     // ActionMove_TeleportToPoint (Train 35 blink)
#include "Game/NinjaRun.h"        // Train 37: dash burst flag refresh
#include "globals.h"              // Hero, InputEnable, DAT_083a7c24
#include <SDL3/SDL.h>             // SDL_GetTicks
#include <cmath>

CActionJump gActionJump;

namespace {
    // Train 35: the Train 24/29 feel tunables (peak/airtime/squash/
    // glide) are gone with the jump. The blink has no dials: range is
    // the dash system's (Bounce 13/14), arrival is instant (Train 23).

    CHARACTER* HeroC() { return (CHARACTER*)(uintptr_t)Hero; }

    bool SpaceHeld()
    {
        return ((unsigned short)Platform::GetInput().KeyState(VK_SPACE) & 0x8000) != 0;
    }
}

void CActionJump::Update()
{
    CHARACTER* hero = HeroC();

    // Restore-first: whatever XY offset last step applied comes off
    // before anything else reads the hero's position this step. Game
    // logic only ever sees the true position; the offset exists purely
    // between this call and the frame's character draw. (Train 35:
    // the Z hop offset went away with the jump.)
    if (m_AppliedX != 0.0f || m_AppliedY != 0.0f) {
        if (hero) {
            hero->Object.Position[0] -= m_AppliedX;
            hero->Object.Position[1] -= m_AppliedY;
        }
        m_AppliedX = m_AppliedY = 0.0f;
    }
    if (!hero) { m_SpacePrev = false; m_LastTick = 0; m_SpringInit = false; return; }

    const unsigned now = (unsigned)SDL_GetTicks();
    // MEAMU Train 39 (ANIMTRACE watcher): log every hero animation
    // transition with the state around it + the writer tag.
    {
        AnimTrace::gFrame++;
        CHARACTER* hc = HeroC();
        if (hc) {
            static int sLastAction = -1, sLastSkill = -1, sLastStage = -1, sLastCur = -1;
            const int act = (int)hc->Object.CurrentAction;
            const int skl = (int)hc->Skill, stg = (int)hc->SkillStage, cur = (int)hc->CurrentSkill;
            if (sLastAction >= 0 && (act != sLastAction || skl != sLastSkill || stg != sLastStage || cur != sLastCur)) {
                const char* w = (AnimTrace::gSetActionMark == AnimTrace::gFrame) ? "SetAction"
                              : (AnimTrace::gDirectWriter[0] ? AnimTrace::gDirectWriter : "UNKNOWN");
                char tb[224];
                snprintf(tb, sizeof(tb), "ANIMTRACE: action %d->%d skill %d->%d stage %d->%d cur %d->%d writer=%s",
                    sLastAction, act, sLastSkill, skl, sLastStage, stg, sLastCur, cur, w);
                DbgLogPublic(tb);
                AnimTrace::gDirectWriter = "";
            }
            sLastAction = act; sLastSkill = skl; sLastStage = stg; sLastCur = cur;
        }
    }
    // MEAMU Train 37: the pure-dash burst flag is derived here per
    // frame (the drive tick refreshes it per step too) so it can
    // never stick on across a scheme flip or a closed UI.
    NinjaRun::gDashBurst = (NinjaRun::gDashBurstEndMs != 0 &&
                            (int)(now - NinjaRun::gDashBurstEndMs) < 0);
    if (!NinjaRun::gDashBurst) NinjaRun::gDashBurstEndMs = 0;
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

    // MEAMU Train 35 — SPACE = FACING TELEPORT (Action mode). Gate:
    // Action scheme + no text/UI focus (the pair the WASD drive gates
    // on; Train 30's lesson stands — the pointer lock is a camera
    // concern, not a verb condition). One blink per physical press.
    // Facing: while locked the hero faces the camera aim (Train 22);
    // unlocked, his own facing. The target is a far point along the
    // facing line (24 tiles out) — ActionMove_TeleportToPoint clamps
    // it to the dash range along the line, and the server steps back
    // to the last walkable tile where the landing is blocked
    // (Bounce 14): max range, never a failed cast, no animation
    // (Train 23's instant arrivals cover the mode-2 wire).
    // MEAMU Train 48: target-lock break conditions, per frame.
    TargetLock_Tick();

    // MEAMU Train 55 (2026-11-19, G's order): Q = TARGET LOCK
    // engage/cycle on keyboard+mouse (middle mouse and pad LB
    // remain the other two doors into the same verb). Same gates
    // as Space: Action scheme, no chat/text focus.
    {
        static bool s_QPrev = false;
        const bool qHeld = ((unsigned short)Platform::GetInput().KeyState(0x51) & 0x8000) != 0;
        const bool qEdge = qHeld && !s_QPrev;
        s_QPrev = qHeld;
        if (qEdge && gActionCamera.SchemeIsAction()
            && InputEnable == 0 && DAT_083a7c24 == 0) {
            TargetLock_Engage();
        }
    }

    // MEAMU Train 56 (2026-11-19): F = FISHING verb (cast / reel),
    // both schemes -- the server validates the Lorencia bridge spot.
    {
        static bool s_FPrev = false;
        const bool fHeld = ((unsigned short)Platform::GetInput().KeyState(0x46) & 0x8000) != 0;
        const bool fEdge = fHeld && !s_FPrev;
        s_FPrev = fHeld;
        if (fEdge && InputEnable == 0 && DAT_083a7c24 == 0) {
            Fishing_Verb();
        }
    }

    if (spaceEdge && gActionCamera.SchemeIsAction()
        && InputEnable == 0 && DAT_083a7c24 == 0) {
        // MEAMU Train 48: with a lock, Space blinks BESIDE the locked
        // target (the lock sheet); without one, the facing blink.
        if (TargetLock_Active()) {
            ActionMove_BlinkToTarget(TargetLock_Index());
        } else {
            const float yawDeg = gActionCamera.PointerLocked()
                ? gActionCamera.GetYaw() : hero->Object.Angle[2];
            const float rad = yawDeg * (3.14159265f / 180.0f);
            const float fx = sinf(rad), fy = cosf(rad);
            const int hx = (int)hero->PositionX, hy = (int)hero->PositionY;
            const int tx = hx + (int)lroundf(fx * 24.0f);
            const int ty = hy + (int)lroundf(fy * 24.0f);
            ActionMove_TeleportToPoint(tx, ty);
        }
    }

    if (offX != 0.0f || offY != 0.0f) {
        hero->Object.Position[0] += offX;
        hero->Object.Position[1] += offY;
        m_AppliedX = offX; m_AppliedY = offY;
    }
}

// MEAMU Train 29 built these for the hop's air state (air-dash glide /
// teleport air-cancel). Train 35 removed the jump: there is no air
// state, so both are no-ops. The call sites in ActionMove stand —
// a dash or teleport simply has no air to convert or cancel. The
// GunZ chain lives on the ground now: dash (no cooldown) and the
// Space blink extend each other in any order.
void CActionJump::OnAirDash() {}
void CActionJump::CancelAir() {}
