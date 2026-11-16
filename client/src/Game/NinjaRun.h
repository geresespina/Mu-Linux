#pragma once

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
    inline unsigned short ScaleReportedSpeed(unsigned short serverSpeed)
    {
        const unsigned int scaled = (unsigned int)((float)serverSpeed * kSpeedFactor);
        return (unsigned short)(scaled > 0xFFFFu ? 0xFFFFu : scaled);
    }
}
