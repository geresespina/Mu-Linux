#pragma once
// TargetLock.h — MEAMU Train 48 (2026-11-19): TARGET LOCK for
// Action mode (G's build order, lock sheet as ruled).
//
// One targeting model: the lock RIDES KbPlay's existing Tab lock
// (gLockIdx) — AcquireTarget already resolves LOCK -> last target
// -> nearest for every cast path (digit holds, AUTO, pad mirrors).
// This module adds the Action-mode verb (middle mouse / pad LB),
// aim-first engagement, and the break conditions the Tab lock
// never had. Tab cycling in Classic is untouched: verb locks are
// tracked by an engaged flag and only they get the break rules.
//
// Engage (Action only): lock the latch-legal enemy nearest the
// aim (ReticlePick first); pressing again with the same target
// under the aim — or no fresh pick — CYCLES to the next enemy by
// distance; no candidates at all CLEARS. Breaks: target dead /
// gone, target beyond 1.5x engage range (22 tiles), line of
// sight lost for 2 s, hero dead, scheme leaves Action.
// While locked: hero facing eases to the target (ActionCamera),
// casts resolve to it (AcquireTarget), Space / pad-B blink lands
// beside it (ActionMove_BlinkToTarget). Camera never rotates.

void TargetLock_Engage();
// MEAMU Train 75 (G's pad ruling, 2026-11-20): LB is the lock
// ON/OFF toggle -- press with no lock standing engages (the
// Engage rules above); press with a lock standing RELEASES it.
// Not a cycle. Q keeps its engage/cycle behavior; Toggle is the
// pad's verb. Action semantics live in TargetLock_Toggle();
// Gamepad's Classic branch toggles KbPlay's plain lock with the
// two bridges below (declared here for its use).
void TargetLock_Toggle();
int  KbPlay_LockIndex();
void KbPlay_LockClear();
// MEAMU Train 64: PAD-CAST AUTO-LOCK (G's ruling). Fills the lock
// ONLY when it is empty — a manual lock (Tab cycle or the LB/Q
// verb) always wins until it breaks on its own rules. Monsters
// only (manual engage keeps players latch-legal), closest within
// engage range. In Action the fill is a verb lock (break rules +
// facing); in Classic it is a plain Tab-style lock (KbPlay's own
// housekeeping), because verb locks are Action-only by design.
// Returns true when it set a lock.
bool TargetLock_AutoLockClosest();
// MEAMU Train 76 (G's lock rule): a KILL clears the lock with
// NO automatic re-fill -- after the locked target dies, neither
// the LT-hold re-engage (Train 72) nor the pad-cast auto-lock
// (Train 64) may pick the next target; the next lock is a
// deliberate LB / Q / fresh LT press. Range and LoS breaks are
// NOT kills and keep their mid-hold re-fill.
bool KbPlay_SelectedSkillTargeted();
int  KbPlay_NearestMonsterInRange(int rangeTiles);   // Train 80   // Train 70 bridge (impl in KbPlay.cpp)
// MEAMU Train 68: HOLD-LT verb (G's order) -- while LT is held the
// lock auto-engages the closest candidate (monsters included).
// Gamepad feeds the raw LT state every pad tick;
// TargetLock_LtHold edge/state lives here. Train 74 generalised
// the camera follow: it is a property of ANY standing lock, so
// the ActionCamera reads TargetLock_Active() directly and
// FollowHeld remains only as LT's held-state record.
void TargetLock_LtHold(bool held);
// MEAMU Train 78 (G's LT extension): the LT hold HUNTS -- with
// no lock standing it seeks the closest latch-legal target in
// the whole visible list (no range cap on the search); too far
// out, it chain-teleports toward the target (8-tile hops aimed
// to land just inside engage range) until the fill can lock.
// The Train 76 kill latch gates the hunt exactly like the fill:
// a kill mid-hold stops the seeking until a fresh LT press.
bool TargetLock_FollowHeld();
void TargetLock_Tick();
bool TargetLock_Active();
int  TargetLock_Index();
void TargetLock_Clear();
