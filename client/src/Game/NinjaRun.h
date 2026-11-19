#pragma once

// ── MEAMU Train 51 ROLLBACK (2026-11-19, G's final order) ───────────────────
// "Put it back to what it was BEFORE we broke it." The movespeed
// increase is REMOVED wholesale: no call site scales speed any
// more (Terrain step rate, BMD_Anim cadence and the C3/0E report
// are stock; the lean is gone; the GS allowance is disabled and
// Bounce 20's walk-sync is retired). This header survives only as
// the record + the inert dash-burst flag the detector still sets
// (it drives nothing). The baseline restored is Nov 14, 2026 —
// the last day before the module below landed.
// ── MEAMU NINJA RUN (2026-11-15, G's order: "Ninja Run for everyone") ────────
// Always-on run replacement for every player character: the ground step rate
// (CharacterMoveSpeed), the walk/run animation cadence (CharacterAnimation)
// and the C3/0E PhysiSpeed report (Game_MainLoop liveclient) all scale by the
// SAME factor, and the posed model pitches forward while the character is in
// a locomotion action (ObjectPose pose prep, render-only: Object.Angle is
// never written, so combat/effect code that reads Angle[0] is unaffected).
//
// GS contract (LIVE since 2026-11-15 18:04, GS sha 0f7c6983,
// Data/Custom/NinjaRun.txt Enable 1, SpeedFactorPercent 200): a PhysiSpeed
// report up to server value x200% + the stock flat tolerance is legitimate
// (one [NinjaRun] audit line per character slot); above the cap the stock
// speed-hack kick fires unchanged. kSpeedFactor = 1.75 keeps a 12.5% margin
// under the cap on every input (1.75x <= 2.0x by construction, and the echoed
// F3/E1 value this client reports from has never tripped the check at 1.0x).
// MagicSpeed is NEVER scaled — its GS check has no allowance.
//
// TRIM (2026-11-16, G's order): the factor shipped at 1.9 on 2026-11-15; at
// 1.9 the step rate sat close enough to the server allowance that players
// got pulled back on long runs, so G ordered it lowered a bit. 1.75 lives
// ONLY here — the step rate (Terrain), the animation cadence (BMD_Anim) and
// the C3/0E report scale (Game_MainLoop, via ScaleReportedSpeed below) all
// read this one constant, so the system can never desync against itself.
//
// Arms-pinned-back is baked model animation and is out of scope by design.
namespace NinjaRun
{
    inline constexpr float kSpeedFactor = 1.75f;
    inline constexpr float kLeanDegrees = 18.0f;

    // Player locomotion actions (the walk/run family). Same set the footstep
    // sounds gate on in MoveCharacterVisual: 13..33 + 79 + 80.
    inline bool IsLocomotionAction(unsigned char action)
    {
        return (action >= 13 && action <= 33) || action == 79 || action == 80;
    }

    // C3/0E report scaling, clamped to the WORD the packet carries.
    inline unsigned short ScaleReportedSpeed(unsigned short serverSpeed, float factor)
    {
        const unsigned int scaled = (unsigned int)((float)serverSpeed * factor);
        return (unsigned short)(scaled > 0xFFFFu ? 0xFFFFu : scaled);
    }
    inline unsigned short ScaleReportedSpeed(unsigned short serverSpeed)
    {
        return ScaleReportedSpeed(serverSpeed, kSpeedFactor);
    }

    // MEAMU Train 37 (2026-11-19): PURE DASH — G's ruling: double-tap
    // WASD is a fast movement burst, the teleport stripped out. During
    // the burst window the HERO's step rate (Terrain), locomotion
    // cadence (BMD_Anim) and C3/0E report (Game_MainLoop) all scale by
    // kDashFactor instead of kSpeedFactor, so the system stays in
    // sync against itself exactly like the base run. GS contract:
    // Bounce 15 raises NinjaRun.txt SpeedFactorPercent 200 -> 340, so
    // the burst report (300%) keeps the same under-cap margin as
    // 1.75 under 200. The window end is written by ActionMove's
    // DashScan (SDL ticks); the live flag is refreshed per frame by
    // ActionJump::Update and per step by the drive tick.
    inline constexpr float kDashFactor = 4.0f;   // Train 47 (2026-11-19): G CANCELLED the revert — x4.0 STAYS (Train 46 superseded)   // Train 44 (2026-11-19): G's x4.0 dash tune (Bounce 18 ceiling x4.60)
    inline unsigned gDashBurstEndMs = 0;   // SDL tick the burst ends (0 = none)
    inline bool     gDashBurst = false;    // derived live flag
    inline float ActiveFactor() { return gDashBurst ? kDashFactor : kSpeedFactor; }
}
