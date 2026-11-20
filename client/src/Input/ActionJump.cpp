// ActionJump.cpp — MEAMU Train 24 built SPACE = JUMP here; Train
// 35 (2026-11-19) replaced it with the facing teleport. MEAMU
// Train 82 (2026-11-20), G's keyboard-parity order: Space in
// Action mode is now LT's verb -- HOLD-TO-AIM TELEPORT TARGETING
// (hold raises the marker, mouse-look aims it, release lands);
// the instant facing blink is REPLACED on the keyboard (the pad
// keeps its blink on R3). This module also drives RMB = RT's
// verb (the triple verb + hunt) for the same parity.
// What remains: the Train 28 smoothing spring and the verb
// drivers. See ActionJump.h.

#include "stdafx.h"
#include "Input/ActionJump.h"

#include "Platform/IInput.h"      // Platform::GetInput (physical key state)
#include "Entity/EntityTypes.h"   // CHARACTER
#include "Render/ActionCamera.h"  // gActionCamera (lock + scheme state)
#include "Input/ActionMove.h"
#include "Input/AnimTrace.h"
#include "Input/TargetLock.h"   // Train 48
#include "Input/Fishing.h"      // Train 56: fishing verb   // MEAMU Train 39 diagnostic     // ActionMove_TeleportToPoint (Train 35 blink)
#include "Input/Gamepad.h"      // Train 82: TpTarget verbs (shared with the pad)
namespace Platform { void InjectVirtualKey(int vk, bool down); }   // Train 82: the '2' mirror for RMB
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
    const bool spaceRelease = !space && m_SpacePrev;
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

    // MEAMU Train 84 (2026-11-20, G's ruling): Q = LB. Q is now
    // the lock ON/OFF TOGGLE (Train 75), in both schemes --
    // no lock -> engage; lock standing -> release. Train 55's
    // engage/cycle on Q is REPLACED (cycling survives nowhere
    // on the toggle doors; the hunt/RT-RMB fill and the cast
    // auto-lock are untouched). The Classic arm calls the very
    // same code LB calls (clear, or Gamepad's queued Tab
    // pulse), so Q and LB can never drift apart. Same gates
    // as the other verb drivers: no chat/text focus.
    {
        static bool s_QPrev = false;
        const bool qHeld = ((unsigned short)Platform::GetInput().KeyState(0x51) & 0x8000) != 0;
        const bool qEdge = qHeld && !s_QPrev;
        s_QPrev = qHeld;
        if (qEdge && InputEnable == 0 && DAT_083a7c24 == 0) {
            if (gActionCamera.SchemeIsAction()) {
                TargetLock_Toggle();
            } else if (KbPlay_LockIndex() >= 0) {
                KbPlay_LockClear();
            } else {
                PulseKeyShared(0x09, "Q lock on (Tab)");
            }
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

    // MEAMU Train 82 (2026-11-20, G's keyboard-parity order:
    // "Spacebar = same as LT · RMB = same as RT"): Space in
    // Action mode is LT's verb -- HOLD-TO-AIM TELEPORT
    // TARGETING. Hold raises the marker (Train 59/66 state,
    // shared with the pad), mouse-look aims it (the marker
    // rides the camera aim at full blink range), release
    // lands. This REPLACES the Train 35 instant facing blink
    // -- and Train 48's Space blink-to-lock with it -- on the
    // KEYBOARD; the pad keeps its blink on R3, and the
    // double-click teleport is untouched. Classic Space (the
    // KbPlay attack latch) is untouched: this block is
    // Action-only, like the blink it replaces.
    if (gActionCamera.SchemeIsAction()) {
        const bool gates = (InputEnable == 0 && DAT_083a7c24 == 0);
        if (spaceEdge && gates) TpTargetBegin();
        if (space && TpTargetActive()) TpTargetUpdate(dt, true);
        if (spaceRelease) TpTargetFire();   // no-op when not targeting
        if (!gates && TpTargetActive() && !space) TpTargetCancel("gates closed");
    }

    // MEAMU Train 82: RMB in Action mode is RT's verb -- hold =
    // auto-target + camera follow + SKILL 2 CAST-HOLD + the
    // hunt (TargetLock_LtHold + the '2' mirror, the exact pair
    // the pad's RT drives since Train 81). Release: the cast
    // stops, the lock stays, the follow ends (Train 72/68
    // semantics, via LtHold(false)). Same gates as Q/F above.
    {
        static bool s_RmbPrev = false;
        const bool rmb = ((unsigned short)Platform::GetInput().KeyState(0x02) & 0x8000) != 0;   // VK_RBUTTON
        const bool gates = gActionCamera.SchemeIsAction()
            && InputEnable == 0 && DAT_083a7c24 == 0;
        const bool drive = rmb && gates;
        if (drive != s_RmbPrev) {
            Platform::InjectVirtualKey(0x32, drive);   // '2' cast-hold mirror
            TargetLock_LtHold(drive);
            s_RmbPrev = drive;
        } else if (drive) {
            TargetLock_LtHold(true);   // per-frame: fill / follow / hunt
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
