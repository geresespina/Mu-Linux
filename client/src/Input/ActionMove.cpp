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

// ── Bounce 13 dash wire — STUB ───────────────────────────────────────────────
// GG INTEGRATION POINT: the GS worker's census owns the final packet
// code (DASH-WIRE.md, builds/bounce13-dash-2026-11-19 — not landed when
// this was built). Set kDashWireCode to the final C1 code and the two
// request shapes below go live unchanged:
//   direction dash: [C1][05][code][0][dir]        dir = world octant 0..7
//   blink-to-target:[C1][06][code][1][keyHi][keyLo]
// While kDashWireCode == 0 the requests are detected and logged but NO
// packet leaves the client (dash/blink are inert, movement is not).
constexpr unsigned char kDashWireCode = 0;
constexpr unsigned char kDashModeDirection = 0;
constexpr unsigned char kDashModeTarget    = 1;

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

// ── Dash / blink wire (STUB — see kDashWireCode) ─────────────────────────────
bool SendDashRequest(unsigned char mode, int keyOrDir)
{
    if (kDashWireCode == 0) {
        ActLog("ACTION-WIRE: %s request (arg %d) — wire code TBD (Bounce 13), dropped",
               mode == kDashModeTarget ? "blink-to-target" : "dash", keyOrDir);
        return false;
    }
    if (mode == kDashModeTarget) {
        const BYTE pkt[6] = { 0xC1, 0x06, kDashWireCode, kDashModeTarget,
                              (BYTE)((keyOrDir >> 8) & 0xFF), (BYTE)(keyOrDir & 0xFF) };
        gNetwork.SendC1(pkt, sizeof(pkt));
    } else {
        const BYTE pkt[5] = { 0xC1, 0x05, kDashWireCode, kDashModeDirection,
                              (BYTE)(keyOrDir & 0xFF) };
        gNetwork.SendC1(pkt, sizeof(pkt));
    }
    return true;
}

// World octant index (0..7) of a tile delta, for the dash wire byte.
// Convention: 0 = +Y, then clockwise in 45° steps viewed with +X right /
// +Y up (1 = +X+Y, 2 = +X, 3 = +X−Y, 4 = −Y, 5 = −X−Y, 6 = −X, 7 = −X+Y).
// GG/GS agree the final mapping when kDashWireCode is wired (the request
// is inert until then). NOTE: Movement.h's Entity_GetDirCode is a
// declared-but-never-defined decompile stub (and returns an FPU angle
// code, not an octant) — the engine's real, defined angle helper is
// CreateAngle (functions.h), used for the facing write in the drive.
int OctantOf(int dx, int dy)
{
    if (dx == 0 && dy > 0)  return 0;
    if (dx > 0  && dy > 0)  return 1;
    if (dx > 0  && dy == 0) return 2;
    if (dx > 0  && dy < 0)  return 3;
    if (dx == 0 && dy < 0)  return 4;
    if (dx < 0  && dy < 0)  return 5;
    if (dx < 0  && dy == 0) return 6;
    return 7;   // dx<0, dy>0
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
                ActLog("ACTION: dash (slot %d, octant %d)", s, oct);
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
           attackRangeTiles, sent ? "sent" : "— wire pending");
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

    if (!ActionMove_IsAction()) { ReleaseDrive(); return; }
    if (!DriveGates() || Gamepad_IsDriving()) { ReleaseDrive(); return; }

    // ── Held direction (screen space: x right, y down) ───────────────────────
    const int wantX = (KeyHeld(0x44) || KeyHeld(39) ? 1 : 0) -
                      (KeyHeld(0x41) || KeyHeld(37) ? 1 : 0);
    const int wantY = (KeyHeld(0x53) || KeyHeld(40) ? 1 : 0) -
                      (KeyHeld(0x57) || KeyHeld(38) ? 1 : 0);

    // Double-tap -> dash. A press consumed by a dash must not also walk:
    // while EVERY held direction slot is consumed, the drive stays off.
    DashScan(now, wantX, wantY);
    bool anyHeld = false, anyLive = false;
    for (int s = 0; s < 4; ++s) {
        if (!gTaps[s].held) continue;
        anyHeld = true;
        if (!gTaps[s].consumed) anyLive = true;
    }
    if (!anyHeld || (wantX == 0 && wantY == 0) || !anyLive) {
        ReleaseDrive();
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
    } else {
        // Projection degenerate (should not happen): NavMove's screen park.
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
