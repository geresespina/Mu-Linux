// ActionMove.cpp — MEAMU Train 13 (2026-11-19): the ACTION control
// scheme. See ActionMove.h for the full design. In short:
//
//   * Scheme state is CUserSettings' ([Controls] Scheme, persisted);
//     this module caches it, flips it on F5, and drives only in ACTION.
//   * WASD/arrows steer through a virtual cursor parked on the ground
//     point 2 tiles ahead in the held direction + a held virtual left
//     button (the NavMove/KbPlay rails): the game's hold-to-move issues
//     the steps. The direction is CAMERA-RELATIVE by construction: the
//     8 world octants around the hero are projected to screen with the
//     live camera (Camera_ProjectWorldToScreen) every tick and the one
//     whose screen direction best matches the held keys wins — rotate
//     the camera (F10 3D view) and W follows the view.
//   * The hero faces the movement octant while driving.
//   * The player's own mouse is AIM: if the real cursor has moved away
//     from this module's parked point, parking yields for a grace beat
//     so hover targeting (SelectedCharacter) stays the player's.
//   * Double-tap a direction (<= 300 ms release->press, first tap
//     <= 350 ms) = DASH request to the server; the tap's walk step is
//     suppressed for the rest of that press. Server-executed only.
//   * TryBlinkEngage(): the shared blink-engage hook (both schemes).

#include "stdafx.h"
#include "Input/ActionMove.h"
#include "Config/UserSettings.h"  // gUserSettings ([Controls] Scheme)
#include "Input/Gamepad.h"        // Gamepad_IsDriving (mutual yield)
#include "Platform/IInput.h"      // Platform::InputEvent, GetInput
#include "Core/Window.h"          // gWindow
#include "Entity/EntityTypes.h"   // CHARACTER, CHARACTER_STRIDE
#include "Entity/EntityName.h"    // EntityName::Get
#include "functions.h"            // Camera_ProjectWorldToScreen, CreateAngle
#include "Net/Net.h"              // gNetwork
#include "Render/ActionCamera.h"  // Train 15: strafe facing law (gActionCamera)
#include <SDL3/SDL.h>             // SDL_GetTicks
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" void DbgLogPublic(const char* msg);

namespace Platform {
void DeliverSDLInput(const InputEvent& event);   // SDLWindow.cpp
}

namespace {

// ── Tuning ───────────────────────────────────────────────────────────────────
constexpr int   kSchemeVk     = 0xC0;   // VK_OEM_3 — the GRAVE/backtick
                                        // key, scheme flip (G's pick,
                                        // Train 13 amendment 4). Census:
                                        // NO KeyHeld/KeyState consumer of
                                        // 0xC0 exists anywhere in the tree
                                        // (every other 0xC0 hit is a data
                                        // offset, model/sound id or packet
                                        // byte), and Win32 delivers it
                                        // natively as VK_OEM_3. The SDL
                                        // mapper did not route the key at
                                        // all (VirtualKey had no GRAVE
                                        // case -> 0); Train 13 adds that
                                        // one case in SDLInput.cpp. (F5
                                        // was the interim pick and stays
                                        // free/unbound: one key, one job.)
constexpr float kNavRadius    = 0.22f;  // of min(winW, winH), as NavMove
constexpr int   kStepTiles    = 2;      // lookahead the cursor parks at
constexpr unsigned kDashWindowMs = 300; // release -> second press
constexpr unsigned kTapMaxMs     = 350; // longest first press still a tap
constexpr unsigned kAimGraceMs   = 500; // park yield after the player
                                        // moves the real mouse

// ── Bounce 13 dash wire — LIVE (Train 15 integration) ────────────────────────
// Authoritative spec: builds/bounce13-dash-2026-11-19/DASH-WIRE.md.
// One C1 frame, 7 bytes: [C1][07][F3][FD][mode][b5][b6] — head 0xF3,
// subcode 0xFD (census-proven free both directions; the F3 wildcard in
// HackPacketCheck.txt passes it with no data change).
//   mode 0 (direction):     b5 = engine Dir 0..7, b6 = 0
//   mode 1 (dash-to-target): b5/b6 = target server index HB/LB
// The GS answers with the stock teleport notification (C3 1C) on success
// and silence on refusal; the client falls back to walking (blink) or
// simply does not move (direction dash). NO cooldown, no mana, all classes.
constexpr unsigned char kDashHead = 0xF3;
constexpr unsigned char kDashSub  = 0xFD;
constexpr unsigned char kDashModeDirection = 0;
constexpr unsigned char kDashModeTarget    = 1;
constexpr unsigned char kDashModeClickTp   = 2;   // Train 17 (Feature 3):
                                        // double-click ground teleport --
                                        // b5 = landing tile X, b6 = tile Y
                                        // (map coords, one byte each).
constexpr int   kClickTpRangeTiles = 8; // clamp ceiling for mode 2 = the
                                        // GS dash MaxRange (Dash.txt,
                                        // Bounce 13). G's clamp rule: a
                                        // click beyond range lands at the
                                        // furthest tile of the range along
                                        // the hero->click line.

// ── State ────────────────────────────────────────────────────────────────────
bool          gInit       = false;
ControlScheme gScheme     = ControlScheme::Classic;
bool          gGravePrev  = false;
unsigned      gFlipSerial = 0;   // ++ on every flip (Train 14 hook)

bool  gBtnDown   = false;               // this module's virtual LMB state
float gParkX = 0.0f, gParkY = 0.0f;     // last parked cursor (window px)
bool  gParkValid = false;
unsigned gAimYieldT = 0;                // last time the player took the mouse

// Double-tap tracking, per direction slot (0=fwd 1=back 2=left 3=right;
// each slot is fed by its WASD key AND its arrow key).
struct TapSlot {
    bool     held       = false;
    unsigned pressT     = 0;   // when the current press began
    unsigned releaseT   = 0;   // when the last tap ended
    unsigned pressDur   = 0;   // how long that tap was held
    bool     consumed   = false;  // this press became a dash: no walk
};
TapSlot gTaps[4];
constexpr int kSlotVk[4][2] = { { 0x57, 38 },   // W / Up
                                { 0x53, 40 },   // S / Down
                                { 0x41, 37 },   // A / Left
                                { 0x44, 39 } }; // D / Right

int gBlinkKey = -1;   // network Key of the target already blink-attempted

void ActLog(const char* fmt, ...)
{
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    DbgLogPublic(buf);
    fprintf(stderr, "%s\n", buf);
    fflush(stderr);
}

void Say(const char* fmt, ...)
{
    char buf[160];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UIChatLogWindow_AddText(nullptr, buf, 1);
    ActLog("ACTION: %s", buf);
}

void SendMouse(Platform::InputEventType type, float x, float y)
{
    Platform::InputEvent ev{};
    ev.type = type;
    ev.x = (int)lroundf(x);
    ev.y = (int)lroundf(y);
    Platform::DeliverSDLInput(ev);
}

void SetButton(bool down)
{
    if (down == gBtnDown) return;
    gBtnDown = down;
    SendMouse(down ? Platform::InputEventType::LeftDown
                   : Platform::InputEventType::LeftUp, gParkX, gParkY);
}

void ParkCursor(float x, float y)
{
    gParkX = x; gParkY = y; gParkValid = true;
    SendMouse(Platform::InputEventType::MouseMove, x, y);
}

void ReleaseDrive()
{
    SetButton(false);
    gParkValid = false;
}

bool KeyHeld(int vk)
{
    return ((unsigned short)Platform::GetInput().KeyState(vk) & 0x8000) != 0;
}

CHARACTER* HeroC() { return (CHARACTER*)(uintptr_t)Hero; }

CHARACTER* EntryAt(int idx)
{
    return (CHARACTER*)((char*)(uintptr_t)CharactersClient + idx * CHARACTER_STRIDE);
}

int Cheb(CHARACTER* a, CHARACTER* b)
{
    const int dx = std::abs((int)a->PositionX - (int)b->PositionX);
    const int dy = std::abs((int)a->PositionY - (int)b->PositionY);
    return dx > dy ? dx : dy;
}

bool HeroDead() { return Hero != 0 && HeroC()->Dead != 0; }

// The drive's gates: in-world, hero present, chat/text input closed, no
// error modal, hero alive. (The scheme flip has its own, looser gate:
// it must also work standing still with panels closed only.)
bool DriveGates()
{
    return SceneFlag == 5 && Hero != 0 && CharactersClient != 0 &&
           InputEnable == 0 && DAT_083a7c24 == 0 && !HeroDead();
}

// ── Dash brake (MEAMU Train 16, Fix 4 — GunZ rule: dash is an INSTANT
// verb; G's playtest: "runs a few steps first, THEN teleports") ────────
// Armed by SendDashRequest (the one choke point BOTH dash kinds pass
// through) BEFORE the packet goes out, right after ReleaseDrive() has
// killed this module's virtual hold: stop-then-send, never
// send-then-stop. While armed, every synthetic movement source stays
// off: this module's drive (ActionMove_Tick checks it), NavMove's
// arrow/follow drives and Player_InputTick's walk-in move packet (both
// read ActionMove_DashBraking()). The brake clears when:
//   * the hero's tile changes (the teleport landed — with every drive
//     off, nothing of ours can move him first), or
//   * a FRESH direction press edge arrives (the player's new input
//     takes control back; DashScan clears it), or
//   * kDashBrakeMs lapses — the GS refuses SILENTLY (no negative ack on
//     the wire), so without a timeout a refused dash would wedge
//     movement until re-press; on lapse the callers' walk-in fallback
//     resumes exactly as Train 13 designed it.
constexpr unsigned kDashBrakeMs = 600;
bool     gBrakeActive  = false;
unsigned gBrakeArmedMs = 0;
int      gBrakeTileX   = 0;
int      gBrakeTileY   = 0;

void ArmDashBrake(unsigned now)
{
    gBrakeActive  = true;
    gBrakeArmedMs = now;
    if (Hero != 0) {
        gBrakeTileX = (int)HeroC()->PositionX;
        gBrakeTileY = (int)HeroC()->PositionY;
    }
}

// Lazy evaluation: correct no matter which module asks first in a frame.
bool DashBrakeActive(unsigned now)
{
    if (!gBrakeActive) return false;
    if (Hero != 0) {
        CHARACTER* hero = HeroC();
        const int dx = std::abs((int)hero->PositionX - gBrakeTileX);
        const int dy = std::abs((int)hero->PositionY - gBrakeTileY);
        if ((dx > dy ? dx : dy) >= 1) { gBrakeActive = false; return false; }  // landed
    }
    if ((int)(now - gBrakeArmedMs) > (int)kDashBrakeMs) { gBrakeActive = false; return false; }
    return true;
}

// ── Direct drive (MEAMU Train 17 — bugs #5/#6: strafe/backpedal dead) ──────
// While the Action Camera is POINTER-LOCKED, the park-and-hold drive
// can never move the hero: Input_OnEvent's MouseMove case
// (Input_WndProc.cpp) consumes every MouseMove as a camera look delta
// when PointerLocked() and never writes the cursor globals, and
// CActionCamera::Update() pins those globals to screen centre every
// sim step. Player_ProcessInput's ground branch therefore raycasts the
// SAME centre tile for every key — the tile along the camera forward —
// so only "forward" ever produced steps (W looked fine by coincidence;
// A/D/S routed forward too). The octant selection was never broken;
// its output channel was.
//
// Fix: while locked, this module issues the move ITSELF, with the exact
// tail Player_InputTick's ground-click branch uses — Path_FindRoute
// into the hero's own path buffer, MovementType = 0,
// Combat_SendMovePathPacket — aimed at the tile kStepTiles ahead in
// the chosen octant. The virtual button stays UP while locked, so the
// input tick cannot interleave centre routes. Re-issue mirrors
// hold-to-move: on octant change, when the current route runs out
// (Movement == 0), or on a 200 ms refresh once the target tile has
// shifted with the hero; a 1-tile FindRoute fallback covers a blocked
// 2-tile target, and no route at all = the hero stops, exactly like
// clicking into a wall. GS pacing is unchanged (steps pace on
// MoveSpeed server-side; one path packet per re-route, like a click).
constexpr unsigned kDirectRefreshMs = 200;
int      gDirectOx = 0, gDirectOy = 0;     // last issued octant
int      gDirectTx = -1, gDirectTy = -1;   // last issued target tile
unsigned gDirectIssueT = 0;                // last issue / failed attempt
bool     gDirectValid = false;

void ResetDirectDrive()
{
    gDirectValid = false;
    gDirectTx = gDirectTy = -1;
}

// ReleaseDrive + forget the direct-drive route state. Used by the
// tick's early-outs so a fresh press always issues immediately.
void StopDrive()
{
    ReleaseDrive();
    ResetDirectDrive();
}

void DirectDriveIssue(int ox, int oy, unsigned now)
{
    CHARACTER* hero = HeroC();
    const int hx = (int)hero->PositionX;
    const int hy = (int)hero->PositionY;
    if (hero->Movement != 0) {
        // Click-branch parity gate (Player_InputTick IDA gates): while
        // walking, only re-route when the cached route start is still
        // within 2 tiles of the hero's real position.
        const int rx = (int)(hero->Object.Position[0] * 0.01f);
        const int ry = (int)(hero->Object.Position[1] * 0.01f);
        if (std::abs(hx - rx) >= 2 || std::abs(hy - ry) >= 2) return;
    }
    const int tx = hx + ox * kStepTiles;
    const int ty = hy + oy * kStepTiles;
    const bool octChanged = !gDirectValid || ox != gDirectOx || oy != gDirectOy;
    if (!octChanged && (int)(now - gDirectIssueT) < (int)kDirectRefreshMs)
        return;   // same octant, just issued (also caps wall retries)
    if (!octChanged && hero->Movement != 0 && tx == gDirectTx && ty == gDirectTy)
        return;   // already walking a route to this exact target
    unsigned char* pathBuf =
        (unsigned char*)((uintptr_t)hero + EntityOffset(0x354));
    unsigned int ok = Path_FindRoute(hx, hy, tx, ty, pathBuf, 0.0f);
    int usedTx = tx, usedTy = ty;
    if ((char)ok == '\0') {
        usedTx = hx + ox; usedTy = hy + oy;   // 1-tile fallback
        ok = Path_FindRoute(hx, hy, usedTx, usedTy, pathBuf, 0.0f);
    }
    gDirectOx = ox; gDirectOy = oy;
    gDirectTx = usedTx; gDirectTy = usedTy;
    gDirectIssueT = now;
    gDirectValid = true;
    if ((char)ok == '\0') return;             // wall: no route, hero stops
    hero->MovementType = 0;
    Combat_SendMovePathPacket(hero, AsObject((uintptr_t)hero));
}

// ── Dash / blink wire (LIVE — DASH-WIRE.md) ──────────────────────────────────
// Emits exactly: C1 07 F3 FD <mode> <b5> <b6>. For mode 1, keyOrDir is the
// target's server object index (CHARACTER.Key, the viewport/attack-packet
// index) split MAKE_NUMBERW-style: b5 = HB, b6 = LB. For mode 0, keyOrDir
// is the engine Dir byte (see OctantOf) and b6 = 0.
// The ONE choke point every dash frame passes through (modes 0/1 via
// SendDashRequest below, mode 2 via ActionMove_TeleportToPoint).
bool EmitDashFrame(unsigned char mode, BYTE b5, BYTE b6, const char* label)
{
    // MEAMU Train 16 (Fix 4): STOP-THEN-SEND. Kill this module's movement
    // output and arm the dash brake BEFORE the request leaves, so no
    // step can be issued from the fire frame until the teleport lands.
    // (The shipped order sent first and let the tick sort it out after;
    // a second held direction kept the drive alive for the whole round
    // trip — the run-up G felt.)
    ReleaseDrive();
    ArmDashBrake((unsigned)SDL_GetTicks());
    const BYTE pkt[7] = { 0xC1, 0x07, kDashHead, kDashSub, mode, b5, b6 };
    gNetwork.SendC1(pkt, sizeof(pkt));
    ActLog("ACTION-WIRE: %s sent — C1 07 F3 FD %02X %02X %02X",
           label, (unsigned)mode, (unsigned)b5, (unsigned)b6);
    return true;
}

bool SendDashRequest(unsigned char mode, int keyOrDir)
{
    BYTE b5, b6;
    if (mode == kDashModeTarget) {
        b5 = (BYTE)((keyOrDir >> 8) & 0xFF);   // target index HB
        b6 = (BYTE)(keyOrDir & 0xFF);          // target index LB
    } else {
        b5 = (BYTE)(keyOrDir & 0xFF);          // engine Dir 0..7
        b6 = 0;
    }
    return EmitDashFrame(mode, b5, b6,
        mode == kDashModeTarget ? "blink-to-target" : "dash");
}

// World octant of a tile delta, in the ENGINE's Dir numbering — the value
// that, assigned to lpObj->Dir, faces the character that way (DASH-WIRE.md,
// Bounce 13; precedent CustomTrain.cpp kSparDirX/Y). Train 15 VERIFIED this
// table against the wire spec line by line: Train 13's draft table was the
// same cycle rotated one step (its 0 = (0,+1)); the engine's 0 = (−1,+1),
// so every value here is the draft's +1 mod 8. Table (dx,dy) -> Dir:
//   (−1,+1)=0  (0,+1)=1  (+1,+1)=2  (+1,0)=3
//   (+1,−1)=4  (0,−1)=5  (−1,−1)=6  (−1,0)=7
// NOTE: Movement.h's Entity_GetDirCode is a declared-but-never-defined
// decompile stub (and returns an FPU angle code, not an octant) — the
// engine's real, defined angle helper is CreateAngle (functions.h), used
// for the facing write in the drive. OctantOf's only other consumer is
// the drive's best-octant validity check (>= 0), which is numbering-blind.
int OctantOf(int dx, int dy)
{
    if (dx < 0  && dy > 0)  return 0;
    if (dx == 0 && dy > 0)  return 1;
    if (dx > 0  && dy > 0)  return 2;
    if (dx > 0  && dy == 0) return 3;
    if (dx > 0  && dy < 0)  return 4;
    if (dx == 0 && dy < 0)  return 5;
    if (dx < 0  && dy < 0)  return 6;
    return 7;   // dx<0, dy==0
}

// Feed the four direction slots; returns true when a double-tap fired
// this tick (the dash request has been sent). Slot press edges are the
// tap events; a held slot is not a tap.
bool DashScan(unsigned now, int wantX, int wantY)
{
    bool fired = false;
    for (int s = 0; s < 4; ++s) {
        TapSlot& t = gTaps[s];
        const bool held = KeyHeld(kSlotVk[s][0]) || KeyHeld(kSlotVk[s][1]);
        if (held && !t.held) {                       // press edge
            // Train 16 (Fix 4): a FRESH press takes control back from a
            // dash brake armed earlier (strict >: an edge in the same
            // scan that armed the brake — a simultaneous second key —
            // must not clear the brake the firing edge just armed).
            if (gBrakeActive && now > gBrakeArmedMs) gBrakeActive = false;
            t.pressT = now;
            if (t.releaseT != 0 && now - t.releaseT <= kDashWindowMs &&
                t.pressDur <= kTapMaxMs) {
                // Double-tap: dash along the CURRENT combined direction
                // (the tapped slot plus anything else held reads as the
                // player's evident vector; a lone tap = that slot).
                int dx = wantX, dy = wantY;
                if (dx == 0 && dy == 0) { dx = (s == 3) - (s == 2); dy = (s == 1) - (s == 0); }
                const int oct = OctantOf(dx, dy);
                SendDashRequest(kDashModeDirection, oct);
                ActLog("ACTION: dash (slot %d, engine dir %d)", s, oct);
                t.consumed = true;
                t.releaseT = 0;                      // no triple-tap chain
                fired = true;
            }
        } else if (!held && t.held) {                // release edge
            t.releaseT = now;
            t.pressDur = now - t.pressT;
            t.consumed = false;
        }
        t.held = held;
    }
    return fired;
}

void FlipScheme()
{
    const ControlScheme next = (gScheme == ControlScheme::Classic)
                             ? ControlScheme::Action : ControlScheme::Classic;
    gScheme = next;
    ++gFlipSerial;                          // Train 14 edge-detects this
    gUserSettings.SetControlScheme(next);   // persists [Controls] Scheme
    ReleaseDrive();
    Say("Control scheme: %s", next == ControlScheme::Action
        ? "ACTION (WASD + mouse aim)" : "CLASSIC (click to move)");
    ActLog("ACTION: scheme -> %s (grave)", next == ControlScheme::Action ? "ACTION" : "CLASSIC");
}

} // namespace

bool ActionMove_IsAction()
{
    return gInit && gScheme == ControlScheme::Action &&
           SceneFlag == 5 && Hero != 0;
}

bool ActionMove_DashBraking()
{
    return DashBrakeActive((unsigned)SDL_GetTicks());
}

ControlScheme ActionMove_Scheme() { return gScheme; }

unsigned ActionMove_SchemeFlipSerial() { return gFlipSerial; }

bool ActionMove_TryBlinkEngage(int targetIdx, int attackRangeTiles)
{
    if (targetIdx < 0 || targetIdx >= 400 || !Hero || !CharactersClient)
        return false;
    CHARACTER* hero = HeroC();
    CHARACTER* tgt  = EntryAt(targetIdx);
    if (tgt->Object.Live == 0 || tgt->Dead != 0) { gBlinkKey = -1; return false; }
    const int key = (int)tgt->Key;
    if (Cheb(hero, tgt) <= attackRangeTiles) {
        if (gBlinkKey == key) gBlinkKey = -1;   // range closed: re-arm
        return false;
    }
    if (gBlinkKey == key) return false;         // one attempt per engagement
    gBlinkKey = key;
    const bool sent = SendDashRequest(kDashModeTarget, key);
    ActLog("ACTION: blink-engage %s (key %d, dist %d > range %d) %s",
           EntityName::Get((char*)tgt), key, Cheb(hero, tgt),
           attackRangeTiles, sent ? "sent" : "NOT SENT");
    return sent;
}

bool ActionMove_CameraLocked()
{
    return gActionCamera.PointerLocked();
}

// MEAMU Train 17 (Feature 3): double-click ground teleport, dash wire
// mode 2. Player_InputTick's ground-click branch detects the physical
// double-click and hands over the clicked tile; this function clamps
// and sends. BOTH schemes (the dash system is scheme-wide, like blink):
// DriveGates carries no scheme condition. Clamp (G's rule): a click
// beyond kClickTpRangeTiles lands at the furthest tile of the range
// along the hero->click line -- never a failed cast, never a short
// hop; a click inside range lands exactly on the clicked tile.
bool ActionMove_TeleportToPoint(int tileX, int tileY)
{
    if (!DriveGates()) return false;   // in-world, alive, chat/text
                                       // closed, no error modal
    CHARACTER* hero = HeroC();
    const int hx = (int)hero->PositionX;
    const int hy = (int)hero->PositionY;
    const int dx = tileX - hx, dy = tileY - hy;
    const int adx = std::abs(dx), ady = std::abs(dy);
    const int cheb = adx > ady ? adx : ady;
    if (cheb == 0) return false;       // clicked the hero's own tile
    int tx = tileX, ty = tileY;
    bool clamped = false;
    if (cheb > kClickTpRangeTiles) {
        const float s = (float)kClickTpRangeTiles / (float)cheb;
        tx = hx + (int)lroundf((float)dx * s);
        ty = hy + (int)lroundf((float)dy * s);
        clamped = true;
    }
    if (tx < 0 || ty < 0 || tx > 255 || ty > 255) return false;
    const bool sent = EmitDashFrame(kDashModeClickTp, (BYTE)tx, (BYTE)ty,
                                    "click-teleport");
    ActLog("ACTION: click-teleport to (%d,%d)%s", tx, ty,
           clamped ? " (clamped: click was beyond range)" : "");
    return sent;
}

void ActionMove_Tick()
{
    if (!gInit) {
        gInit = true;
        gScheme = gUserSettings.GetControlScheme();
        ActLog("ACTION: module init — control scheme %s (Config.ini [Controls] Scheme; grave flips)",
               gScheme == ControlScheme::Action ? "ACTION" : "CLASSIC");
    }
    const unsigned now = (unsigned)SDL_GetTicks();

    // ── Grave/backtick: scheme flip ──────────────────────────────────────────
    // Same discipline as KbPlay's F9: the press edge counts only with the
    // chat input closed and no error modal up; the flip is immediate and
    // persists. Works in CLASSIC too (that is the point of the key).
    const bool grave = KeyHeld(kSchemeVk);
    if (grave && !gGravePrev && SceneFlag == 5 && Hero != 0 &&
        InputEnable == 0 && DAT_083a7c24 == 0)
        FlipScheme();
    gGravePrev = grave;

    if (!ActionMove_IsAction()) { StopDrive(); return; }
    if (!DriveGates() || Gamepad_IsDriving()) { StopDrive(); return; }

    // ── Held direction (screen space: x right, y down) ───────────────────────
    const int wantX = (KeyHeld(0x44) || KeyHeld(39) ? 1 : 0) -
                      (KeyHeld(0x41) || KeyHeld(37) ? 1 : 0);
    const int wantY = (KeyHeld(0x53) || KeyHeld(40) ? 1 : 0) -
                      (KeyHeld(0x57) || KeyHeld(38) ? 1 : 0);

    // Double-tap -> dash. A press consumed by a dash must not also walk:
    // while EVERY held direction slot is consumed, the drive stays off.
    DashScan(now, wantX, wantY);
    // MEAMU Train 16 (Fix 4): while a dash/teleport is in flight the
    // drive stays OFF even when another direction is still held live —
    // no steps between the request and the landing. When the brake
    // clears on landing, the still-held keys resume the drive on the
    // next tick (correct shooter behaviour); the steps BEFORE the
    // teleport were the bug.
    if (DashBrakeActive(now)) { StopDrive(); return; }
    bool anyHeld = false, anyLive = false;
    for (int s = 0; s < 4; ++s) {
        if (!gTaps[s].held) continue;
        anyHeld = true;
        if (!gTaps[s].consumed) anyLive = true;
    }
    if (!anyHeld || (wantX == 0 && wantY == 0) || !anyLive) {
        StopDrive();
        return;
    }

    // ── Camera-relative octant: project the 8 world steps, pick by dot ──────
    CHARACTER* hero = HeroC();
    const float hx = hero->Object.Position[0];
    const float hy = hero->Object.Position[1];
    int hsx = 0, hsy = 0;
    Camera_ProjectWorldToScreen((float*)hero->Object.Position, &hsx, &hsy);
    const float wl = sqrtf((float)(wantX * wantX + wantY * wantY));
    const float wx = wantX / wl, wy = wantY / wl;

    int   bestOct = -1;
    float bestDot = -2.0f;
    int   bestSx = 0, bestSy = 0;
    int   bestOx = 0, bestOy = 0;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            if (ox == 0 && oy == 0) continue;
            float wp[3] = { hx + (float)(ox * kStepTiles) * 100.0f,
                            hy + (float)(oy * kStepTiles) * 100.0f,
                            hero->Object.Position[2] };
            int sx = 0, sy = 0;
            Camera_ProjectWorldToScreen(wp, &sx, &sy);
            const float dx = (float)(sx - hsx), dy = (float)(sy - hsy);
            const float len = sqrtf(dx * dx + dy * dy);
            if (len < 1.0f) continue;
            const float dot = (dx / len) * wx + (dy / len) * wy;
            if (dot > bestDot) {
                bestDot = dot;
                bestOct = OctantOf(ox, oy);
                bestSx = sx; bestSy = sy;
                bestOx = ox; bestOy = oy;
            }
        }
    }

    const float winW = (float)gWindow.GetRealWidth();
    const float winH = (float)gWindow.GetRealHeight();
    const float radius = kNavRadius * (winW < winH ? winW : winH);

    float parkX, parkY;
    if (bestOct >= 0 && bestDot > 0.3f) {
        // Park at the projected step point, pushed out to the nav radius
        // (projection space is 640x480; the input layer wants window px).
        const float dx = (float)(bestSx - hsx), dy = (float)(bestSy - hsy);
        const float len = sqrtf(dx * dx + dy * dy);
        parkX = winW * 0.5f + (dx / len) * radius;
        parkY = winH * 0.5f + (dy / len) * radius;
        // Face the step: the engine's own angle for this delta (degrees,
        // the same value Entity_FaceTarget/CreateAngle produce in-world).
        // Cast-call like KbPlay: functions.h carries a stale wrong-sig
        // declaration of CreateAngle alongside the true one.
        typedef float (__cdecl *CreateAngleFn)(float, float, float, float);
        hero->Object.Angle[2] = ((CreateAngleFn)&CreateAngle)(hx, hy,
            hx + (float)bestOx * 100.0f, hy + (float)bestOy * 100.0f);
        // MEAMU Train 15 (strafe facing law, G 2026-11-19 12:37 — Vice City
        // TPS): while the action camera is pointer-locked, the hero ALWAYS
        // faces the camera yaw. WASD then strafes / backpedals around the
        // aim instead of turning him into the movement direction. The
        // movement step above is unchanged — only the facing is overridden,
        // with the camera's own convention (facing = 180 - camYaw, the same
        // value ActionCamera's idle ease targets). Camera not locked
        // (Classic, kill switch, cursor UI up) = face-movement stands.
        if (gActionCamera.PointerLocked()) {
            float face = 180.0f - gActionCamera.GetYaw();
            while (face > 180.0f) face -= 360.0f;
            while (face <= -180.0f) face += 360.0f;
            hero->Object.Angle[2] = face;
            // MEAMU Train 17: while locked, the park below can never
            // reach the game (Input_OnEvent eats MouseMove as a look
            // delta; the camera pins the cursor globals to centre) --
            // drive directly instead, and keep the virtual button up
            // so Player_ProcessInput does not interleave routes toward
            // the pinned centre tile. See DirectDriveIssue.
            SetButton(false);
            gParkValid = false;
            DirectDriveIssue(bestOx, bestOy, now);
            return;
        }
    } else {
        // Projection degenerate (should not happen): NavMove's screen park.
        if (gActionCamera.PointerLocked()) { StopDrive(); return; }
        parkX = winW * 0.5f + wx * radius;
        parkY = winH * 0.5f + wy * radius;
    }

    // ── Aim yield: the player's real mouse outranks the park ─────────────────
    // The mouse globals are 640-space; my park is window px. If the real
    // cursor sits somewhere else, the player moved it — do not re-park
    // for a grace beat (their hover aim owns SelectedCharacter). The held
    // button keeps walking toward wherever their cursor rests, which is
    // stock hold-to-move steering.
    bool parkNow = true;
    if (gParkValid) {
        const float park640x = gParkX * 640.0f / winW;
        const float park640y = gParkY * 480.0f / winH;
        if (std::fabs((float)(int)DAT_083a427c - park640x) > 3.0f ||
            std::fabs((float)(int)DAT_083a4278 - park640y) > 3.0f) {
            gAimYieldT = now;
            gParkValid = false;
        }
    }
    if (now - gAimYieldT < kAimGraceMs) parkNow = false;
    if (parkNow) ParkCursor(parkX, parkY);

    // Hold the button (with KbPlay-style clobber repair: Player_InputTick
    // zeroes the shared MouseLButton flag when it decides nobody is
    // clicking, so re-assert the hold whenever the flag is down).
    if (DAT_083a42c4 == 0) gBtnDown = false;
    SetButton(true);
}
