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
constexpr int   kBreakRange    = 22;   // 1.5x engage range
constexpr unsigned kLosGraceMs = 2000; // line-of-sight lost -> break

bool     gEngaged   = false;   // the current gLockIdx is a VERB lock
int      gPrevLockIdx = -1;    // Train 76 kill watcher: last seen lock
bool     gKillLatch   = false; // Train 76: set by a kill-break
unsigned gLosLostT  = 0;       // tick when LoS was first lost (0 = seen)

CHARACTER* HeroC() { return (CHARACTER*)(uintptr_t)Hero; }

bool LosClear(int targetIdx)
{
    // Bresenham over tile centres; a tile blocks sight when its
    // wall byte carries BLOCK (0x04) or HOLLOW (0x08).
    int x0, y0, x1, y1;
    KbPlay_TargetTile(-1, x0, y0);
    KbPlay_TargetTile(targetIdx, x1, y1);
    const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    const int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    for (int guard = 0; guard < 64; ++guard) {
        if (x0 == x1 && y0 == y1) return true;
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
        if (x0 == x1 && y0 == y1) return true;
        const BYTE wall = TerrainWall[((y0 & 0xFF) << 8) | (x0 & 0xFF)];
        if (wall & 0x0C) return false;
    }
    return true;
}

void SetLock(int idx)
{
    KbPlay_LockSet(idx);   // parks + names the target (Tab-cycle pattern)
    gEngaged = (idx >= 0);
    gLosLostT = 0;
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
    gLosLostT = 0;
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

bool TargetLock_AutoLockClosest()
{
    if (!Hero) return false;
    WatchLockKill();                             // Train 76
    if (gKillLatch) return false;                // killed: no auto re-fill
    if (KbPlay_LockIndex() >= 0) return false;   // manual lock wins
    int best = -1, bestD = kEngageRange + 1;
    for (int i = 0; i < 400; ++i) {
        if (!InLockPool(i)) continue;   // Train 93: any attackable kind
        const int d = KbPlay_TargetDist(i);
        if (d < bestD) { bestD = d; best = i; }
    }
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
    WatchLockKill();                             // Train 76
    if (gKillLatch) return;                      // killed mid-hold: no re-engage
    const int range = KbPlay_DigitSkillDist(2);  // Train 88: Skill 2's range
    if (range <= 0) return;
    int best = -1, bestD = 9999;
    for (int i = 0; i < 400; ++i) {
        if (!KbPlay_LatchLegal(i)) continue;
        if (!InLockPool(i)) continue;   // Train 93: any attackable kind (was Train 79 monsters-only)
        const int d = KbPlay_TargetDist(i);
        if (d < bestD) { bestD = d; best = i; }
    }
    if (best < 0 || bestD > range) return;       // nothing in range: no hunt
    if (gActionCamera.SchemeIsAction()) {
        SetLock(best);              // verb lock: break rules + facing
    } else {
        KbPlay_LockSet(best);       // Classic: plain Tab-style lock
    }
}

void TargetLock_Engage()
{
    if (!Hero || !gActionCamera.SchemeIsAction()) return;

    // Candidates: latch-legal enemies within engage range, by distance.
    int idxs[400];
    int n = 0;
    for (int i = 0; i < 400; ++i) {
        if (!KbPlay_LatchLegal(i)) continue;
        if (KbPlay_TargetDist(i) <= kEngageRange) idxs[n++] = i;
    }
    if (n == 0) {                       // aimed at nothing: clear
        if (gEngaged) { TargetLock_Clear(); KbPlay_SayText("Target lock cleared"); }
        return;
    }
    // Distance sort (insertion; n is small in practice).
    for (int a = 1; a < n; ++a) {
        const int iv = idxs[a], dv = KbPlay_TargetDist(iv);
        int b = a - 1;
        while (b >= 0 && KbPlay_TargetDist(idxs[b]) > dv) { idxs[b + 1] = idxs[b]; --b; }
        idxs[b + 1] = iv;
    }

    // Aim first: the reticle pick wins when it names a candidate
    // other than the current lock.
    const int cur = KbPlay_LockIndex();
    const int pick = gActionCamera.ReticlePick();
    if (pick >= 0 && pick != cur) {
        for (int a = 0; a < n; ++a)
            if (idxs[a] == pick) { SetLock(pick); return; }
    }
    // Otherwise cycle: nearest, or the next after the current lock.
    int next = idxs[0];
    if (gEngaged) {
        for (int a = 0; a < n; ++a)
            if (idxs[a] == cur) { next = idxs[(a + 1) % n]; break; }
    }
    SetLock(next);
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
    const unsigned now = (unsigned)SDL_GetTicks();
    // Train 89 (pour continuity): while a teleport is in flight and
    // for 300 ms after the landing, the hero's tile is in flux --
    // range and LoS breaks are SUSPENDED so a held RT pour resumes
    // from the landing tile on the same lock, no re-press. Death
    // breaks (WatchLockKill above) still apply.
    const bool tpGrace = Combat_TeleportInFlight()
        || (Combat_LastTeleportLandMs() != 0 && now - Combat_LastTeleportLandMs() < 300);
    if (!tpGrace && KbPlay_TargetDist(idx) > kBreakRange) { TargetLock_Clear(); return; }
    if (!tpGrace && !LosClear(idx)) {
        if (gLosLostT == 0) gLosLostT = now;
        else if (now - gLosLostT > kLosGraceMs) { TargetLock_Clear(); return; }
    } else {
        gLosLostT = 0;
    }
}
