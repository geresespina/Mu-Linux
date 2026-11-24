// TargetLock.cpp — MEAMU Train 48: see TargetLock.h for the design.
// KbPlay owns gLockIdx + the target predicates; this module owns
// the verb, the engaged flag and the break-condition state.
#include "stdafx.h"
#include "TargetLock.h"

#include <cmath>
#include <cstdlib>
#include <SDL3/SDL.h>

#include "globals.h"              // Hero
#include "Entity/EntityTypes.h"
#include "Render/ActionCamera.h"
#include "Entity/EntityName.h"   // EntityName::Get
#include "Input/ActionMove.h"   // Train 89: teleport flight API (pour grace)

// KbPlay.cpp bridges (Train 48): the lock rides the Tab lock's
// variable + predicates, which live in KbPlay's anonymous namespace.
int  KbPlay_LockIndex();
void KbPlay_LockSet(int idx);
void KbPlay_LockClear();
bool KbPlay_LatchLegal(int idx);
bool KbPlay_IsMonster(int idx);
bool KbPlay_IsHostileUser(int idx);   // Train 93: PK-byte hostility (Combat's oracle)
// Train 93: ONE lock pool for every acquisition fill -- a monster,
// or a user-type body that reads PvP to the hero (phantom bot,
// Akatsuki, rival or outlaw player). Own-clan members never read
// PvP (Bounce 38 law), so they can never enter the pool.
static bool InLockPool(int idx)
{
    return KbPlay_IsMonster(idx) || KbPlay_IsHostileUser(idx);
}
bool KbPlay_SelectedSkillTargeted();   // Train 70   // Train 64
int  KbPlay_TargetDist(int idx);
void KbPlay_TargetTile(int idx, int& tx, int& ty);
void KbPlay_SayText(const char* s);

// Terrain wall bytes for the line-of-sight walk (Combat.cpp pattern).
extern unsigned char* TerrainWall;

namespace {

constexpr int   kEngageRange   = 15;   // tiles, Chebyshev

bool     gEngaged   = false;   // the current gLockIdx is a VERB lock
int      gPrevLockIdx = -1;    // Train 76 kill watcher: last seen lock
bool     gKillLatch   = false; // Train 76: set by a kill-break

CHARACTER* HeroC() { return (CHARACTER*)(uintptr_t)Hero; }

void SetLock(int idx)
{
    KbPlay_LockSet(idx);   // parks + names the target (Tab-cycle pattern)
    gEngaged = (idx >= 0);
    gKillLatch = false;    // a lock standing makes the latch moot
}

// MEAMU Train 76 (G's lock rule, 2026-11-20): WHEN THE LOCKED
// TARGET IS KILLED, THE LOCK CLEARS -- NO AUTO-TARGET OF THE
// NEXT TARGET. The kill ends the engagement; the next lock is
// the player's deliberate act (LB / Q / LT press). The latch
// below suppresses BOTH automatic fills -- Train 72's LT-hold
// mid-hold re-engage and Train 64's pad-cast auto-lock -- after
// a kill-break. Scoped to kill/gone breaks ONLY: range and LoS
// breaks keep Train 72's mid-hold re-fill, and a manual engage
// is never blocked. The latch resets on LT's full release, on
// any lock standing again, and on SetLock (a manual engage).
// The watcher runs at the top of Tick AND of both fill paths,
// so a kill seen by KbPlay's own housekeeping is latched
// before any same-frame fill can fire. It watches the plain
// Tab lock too, so the pad-cast auto-lock obeys the rule in
// Classic as well.
void WatchLockKill()
{
    const int cur = KbPlay_LockIndex();
    if (gPrevLockIdx >= 0 && cur < 0 && !KbPlay_LatchLegal(gPrevLockIdx))
        gKillLatch = true;      // the locked target died (or is gone)
    if (cur >= 0) gKillLatch = false;
    gPrevLockIdx = cur;
}

} // namespace

bool TargetLock_Active()
{
    return gEngaged && KbPlay_LockIndex() >= 0;
}

int TargetLock_Index()
{
    return TargetLock_Active() ? KbPlay_LockIndex() : -1;
}

void TargetLock_Clear()
{
    if (gEngaged) KbPlay_LockClear();
    gEngaged = false;
}

// MEAMU Train 86 (G's rule, 2026-11-20): releasing RT unlocks the
// target. The cast and the camera follow already ended with the
// release; now the lock goes too -- the marker (Train 83), the
// facing and the follow all read the one shared slot, so they end
// together. The slot is shared, so this clears whichever door made
// the lock: RT's own fill, the hunt's fill, or a Q/LB toggle lock
// that was standing while RT was held. Classic locks are plain
// KbPlay locks (Clear only releases a verb engagement), so the
// plain slot is cleared directly as well.
void TargetLock_ReleaseUnlock()
{
    TargetLock_Clear();
    if (KbPlay_LockIndex() >= 0) KbPlay_LockClear();
}

// MEAMU Train 97 (G's ruling): lock acquisition is ONE fill for
// EVERY door -- the closest attackable target to the hero inside
// Skill 2's range (latch-legal + the Train 93 pool), the same
// argmin the RT fill has always run. Aim-pick wins nowhere now:
// middle-mouse, LB/Q (both schemes) and the cast auto-lock all
// acquire through here. Latch rules are untouched: this runs
// only at acquisition; a standing lock is never re-picked or
// stolen. respectKillLatch separates the AUTOMATIC fills (RT
// hold, cast auto-lock: a kill mid-hold never auto-refills,
// Train 76) from a DELIBERATE engage press (middle-mouse / LB /
// Q), which is itself the fresh acquisition.
int TargetLock_ClosestAttackable(bool respectKillLatch)
{
    if (!Hero) return -1;
    WatchLockKill();
    if (respectKillLatch && gKillLatch) return -1;
    const int range = KbPlay_DigitSkillDist(2);
    if (range <= 0) return -1;
    int best = -1, bestD = range + 1;
    for (int i = 0; i < 400; ++i) {
        if (!KbPlay_LatchLegal(i)) continue;
        if (!InLockPool(i)) continue;
        const int d = KbPlay_TargetDist(i);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

bool TargetLock_AutoLockClosest()
{
    if (!Hero) return false;
    if (KbPlay_LockIndex() >= 0) return false;   // manual lock wins
    const int best = TargetLock_ClosestAttackable(true);
    if (best < 0) return false;
    if (gActionCamera.SchemeIsAction()) {
        SetLock(best);              // verb lock: break rules + facing
    } else {
        KbPlay_LockSet(best);       // Classic: plain Tab-style lock
    }
    return true;
}

// ── MEAMU Train 68: HOLD-LT verb ────────────────────────────────
// G's order (2026-11-20): holding LT auto-targets (locks the
// closest) and the camera follows the target while held. LT's ONE
// mapped action is now this verb (the no-dual-action law): Skill 2
// left the pad sheet -- keyboard '2' still casts it, exactly as
// Classic's D-pad potions became keyboard-only in Train 64.
// Behaviour: while held, an empty lock fills with the closest
// latch-legal candidate in engage range (monsters included since
// the Train 68 predicate fix); a standing lock is never stolen; if
// the lock breaks mid-hold the next tick re-engages. The lock
// itself is the ordinary lock (verb lock in Action, plain
// Tab-style in Classic) and PERSISTS after release -- only the
// camera follow is hold-scoped.
bool gLtHeld = false;

bool TargetLock_FollowHeld()
{
    return gLtHeld;
}

// MEAMU Train 88 (G's rule, 2026-11-20): THE HUNT IS REMOVED.
// Trains 78/79 made the held trigger seek targets across the
// field and teleport-chase them hop by hop; G removed that
// behaviour outright. The verb now engages ONLY a target already
// inside the poured skill's range -- the skill on key '2',
// resolved the way the digit itself resolves (KbPlay bridge):
// the closest latch-legal MONSTER inside that range fills the
// lock, the camera follows, the pour runs. Nothing in range =
// no lock, no hops, no seek; the pour still fires on the aim
// (Train 80's guarantee covers its targeting). The Train 76 kill
// latch still gates the fill, and Train 79's monsters-only
// discipline carries over: bystanders are for the manual verbs.
void TargetLock_LtHold(bool held)
{
    gLtHeld = held;
    if (!held) { gKillLatch = false; return; }   // Train 76 latch reset
    if (!Hero) return;
    if (KbPlay_LockIndex() >= 0) return;   // a standing lock wins
    // The shared Train 97 fill: closest attackable to the hero
    // inside Skill 2's range (the kill latch rides inside --
    // a kill mid-hold never re-engages, Train 76).
    const int best = TargetLock_ClosestAttackable(true);
    if (best < 0) return;                        // nothing in range: no hunt
    if (gActionCamera.SchemeIsAction()) {
        SetLock(best);              // verb lock: break rules + facing
    } else {
        KbPlay_LockSet(best);       // Classic: plain Tab-style lock
    }
}

void TargetLock_Engage()
{
    if (!Hero || !gActionCamera.SchemeIsAction()) return;
    // MEAMU Train 97 (G's ruling): middle-mouse acquires the
    // CLOSEST attackable target, exactly like every other door.
    // The aim-pick and the press-to-cycle are gone. A standing
    // lock is never re-picked (the latch + break rules govern
    // its life); a deliberate press ignores the kill latch --
    // the press IS the fresh acquisition.
    if (KbPlay_LockIndex() >= 0) return;
    const int best = TargetLock_ClosestAttackable(false);
    if (best < 0) return;
    SetLock(best);
}

// MEAMU Train 75: LB ON/OFF (Action side). A standing verb lock
// releases; no lock engages by the standing Engage rules. The
// release speaks, mirroring Engage's no-candidates clear.
void TargetLock_Toggle()
{
    if (!Hero || !gActionCamera.SchemeIsAction()) return;
    if (TargetLock_Active()) {
        TargetLock_Clear();
        KbPlay_SayText("Target lock cleared");
    } else {
        TargetLock_Engage();
    }
}

void TargetLock_Tick()
{
    WatchLockKill();                             // Train 76 (both schemes)
    if (!gEngaged) return;
    if (!Hero || !gActionCamera.SchemeIsAction()) { TargetLock_Clear(); return; }
    CHARACTER* hero = HeroC();
    if (hero->Object.Live == 0 || hero->Dead != 0) { TargetLock_Clear(); return; }
    const int idx = KbPlay_LockIndex();
    if (idx < 0 || idx >= 400 || !KbPlay_LatchLegal(idx)) { TargetLock_Clear(); return; }
    // TRAIN 119 (G's lock law, 2026-11-23): THE LOCK NEVER
    // DROPS ON ITS OWN. Train 55's 22-tile range break and
    // the 2 s line-of-sight break are REMOVED -- distance
    // and walls no longer clear a standing lock, ever.
    // What still ends a lock: the target dies or is gone
    // (the latch check above + WatchLockKill), the hero
    // dies, the scheme leaves Action, or the player
    // toggles it off (LB / Q). The follow closes distance;
    // casts land again when the target is back in range.
}
