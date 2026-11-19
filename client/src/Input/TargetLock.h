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
void TargetLock_Tick();
bool TargetLock_Active();
int  TargetLock_Index();
void TargetLock_Clear();
