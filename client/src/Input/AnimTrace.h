// MEAMU Train 39 (2026-11-19) — ANIMTRACE: a diagnostic build for the
// Mangekyo cast-animation hunt (third round). Every change to the
// HERO's CurrentAction is logged with the animation state around it
// and WHO wrote it: SetAction tags itself, the two known direct
// field writers (NetRecv_0x18's SetAnim, NetRecv_0x1A's block) tag
// themselves, and the per-frame watcher in ActionJump::Update
// catches anything else as writer=UNKNOWN. One Mangekyo cast in
// this build names the surviving writer. Remove after the hunt.
#pragma once

namespace AnimTrace {
    inline int         gSetActionMark = -1;   // frame of last SetAction hero write
    inline const char* gDirectWriter = "";    // tag from a direct field writer
    inline int         gFrame = 0;            // watcher frame counter

    inline void NoteSetAction() { gSetActionMark = gFrame; }
    inline void NoteDirect(const char* w) { gDirectWriter = w; }
}

extern "C" void DbgLogPublic(const char*);
