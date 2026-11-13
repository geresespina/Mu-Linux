// Gamepad.cpp — MEAMU rev.5 controller support, FULL build (2026-11-13).
//
// Design: builds/controller-lab-2026-11-12/MAPPING.md. The pad never talks
// to game logic directly — it synthesizes the exact mouse/keyboard input the
// game already consumes (Platform::DeliverSDLInput events through the real
// input path + Platform::InjectVirtualKey into the real key tables), so
// menus, inventory, targeting and casting behave identically to mouse+keys.
// The one exception mirrors what the game's own UI writes: skill selection
// (Hero->CurrentSkill + its DAT_0055a3e4 mirror + the selection click sound),
// exactly as the bottom-bar skill grid writes it in Chat_InputTick.
//
// Mapping (Xbox layout):
//   Left stick    direct move (cursor parked ahead of hero + virtual LMB held)
//   Right stick   virtual cursor (~CursorSpeed px/s)
//   RT            virtual left click (hold mirrors the trigger)
//   LT            virtual right click hold = cast selected skill, sustained
//   LB / RB       cycle selected skill down/up through learned skills
//   LB+RB chord   open the expanded skill list (clicks the grid toggle)
//   A (tap)       Space = pick up / interact    A (hold)  quick skill slot 3
//   X             quick skill slot 1
//   Y (tap)       Inventory (I)                 Y (hold)  quick skill slot 2
//   B             Esc (the client's own back/close key; opens menu if idle)
//   D-pad         Up = Q (HP potion) · Right = W (MP) · Down = E (SD) · Left = C
//   Start         Esc = main menu               Back (tap) = M move list
//   Back (hold)   Tab held = minimap
//   L3            toggle auto-cast latch on the hover target (RMB latch)
//   R3            recenter the virtual cursor on the hero
//
// Quick slot N = the skill whose Ctrl+N hotkey assignment is N (the game's
// own hotkey table), falling back to the Nth learned skill when unassigned.
//
// Config.ini [Gamepad]: Enable (default 1), Deadzone (percent, default 25),
// CursorSpeed (px/s, default 900) — Config/UserSettings.
//
// Sandbox proof: MU_PAD_SELFTEST=1 attaches an SDL *virtual* gamepad and
// scripts an input timeline, logging every step + reaction via DbgLogPublic.
// A real pad takes the identical path (SDL abstracts the transport).

#include "stdafx.h"
#include "Input/Gamepad.h"
#include "Input/NavMove.h"       // NavMove_ArrowsHeld (movement yield)
#include "Platform/IInput.h"     // Platform::InputEvent / InputEventType
#include "Core/Window.h"         // gWindow (real pixel size)
#include "Config/UserSettings.h" // gUserSettings ([Gamepad] section)
#include "Entity/EntityTypes.h"  // CHARACTER (CurrentSkill)
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if !defined(_WIN32)
#include <unistd.h>              // access() — self-test pause file (POSIX rig)
#endif

extern "C" void DbgLogPublic(const char* msg);

namespace Platform {
void DeliverSDLInput(const InputEvent& event);   // SDLWindow.cpp
void InjectVirtualKey(int vk, bool down);        // SDLInput.cpp (rev.5 export)
}

namespace {

// ── Tuning (MAPPING.md; deadzone + cursor speed come from Config.ini) ───────
constexpr int   kTriggerOn   = 16000;    // of 32767
constexpr float kMoveRadius  = 0.22f;    // of min(winW, winH)
constexpr Uint64 kLongPressMs = 350;     // A/Y tap-vs-hold split
constexpr Uint64 kTabHoldMs   = 400;     // Back tap (M) vs hold (Tab minimap)

// ── State ────────────────────────────────────────────────────────────────────
SDL_Gamepad*   gPad        = nullptr;
SDL_JoystickID gPadId      = 0;
bool           gInitTried  = false;
bool           gEnabled    = true;       // Config.ini [Gamepad] Enable
float          gDeadzone   = 0.25f;
float          gCursorSpd  = 900.0f;
Uint64         gLastMs     = 0;
bool           gDriving    = false;      // stick/trigger activity this frame

float gCursorX = -1.0f, gCursorY = -1.0f;   // window px; <0 = center on first use
bool  gCursorInit = false;

// Virtual mouse buttons have independent owners (trigger / stick-move / UI /
// LT / auto-cast latch) so overlapping holds never pop each other's button.
enum LeftOwner  { kLeftRT = 1, kLeftMove = 2, kLeftUI = 4 };
enum RightOwner { kRightLT = 1, kRightAuto = 2 };
int  gLeftOwners  = 0;
int  gRightOwners = 0;
bool gUiClickUpPending = false;          // chord grid click: up next tick
bool gAutoCast = false;                  // L3 latch

// One-frame key pulses: injected "down" on the press frame, "up" next tick.
struct KeyPulse { int vk; bool upPending; };
KeyPulse gPulses[8];
int      gPulseCount = 0;

// Tap/hold buttons (A and Y): tap fires a key pulse on release; holding past
// kLongPressMs fires a quick-skill selection instead.
struct TapHold {
    SDL_GamepadButton btn;
    bool     down = false;
    bool     longFired = false;
    Uint64   downMs = 0;
    int      tapVk = 0;
    const char* tapWhat = nullptr;
    int      quickSlot = 0;
};
TapHold gTapA{ SDL_GAMEPAD_BUTTON_SOUTH, false, false, 0, 32,   "Space/pickup",  3 };
TapHold gTapY{ SDL_GAMEPAD_BUTTON_NORTH, false, false, 0, 0x49, "I/inventory",   2 };

// Back button: tap = M, hold = Tab (minimap).
bool   gBackDown = false, gBackTabHeld = false;
Uint64 gBackDownMs = 0;

bool gPrevButtons[SDL_GAMEPAD_BUTTON_COUNT] = {};
bool gPrevRT = false, gPrevLT = false;
bool gPrevChord = false;

// ── Self-test (virtual pad) state ────────────────────────────────────────────
bool           gSelfTest   = false;
SDL_Joystick*  gVirtJoy    = nullptr;
SDL_JoystickID gVirtId     = 0;
Uint64         gSelfTestT0 = 0;
int            gSelfStep   = -1;

void PadLog(const char* fmt, ...)
{
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    DbgLogPublic(buf);
    fprintf(stderr, "%s\n", buf);
    fflush(stderr);
}

void Rumble(Uint16 low, Uint16 high, Uint32 ms)
{
    if (gPad) SDL_RumbleGamepad(gPad, low, high, ms);
}

void SendMouse(Platform::InputEventType type, float x, float y)
{
    Platform::InputEvent ev{};
    ev.type = type;
    ev.x = (int)lroundf(x);
    ev.y = (int)lroundf(y);
    Platform::DeliverSDLInput(ev);
}

void MoveCursorTo(float x, float y, bool force)
{
    if (!force && fabsf(x - gCursorX) < 0.5f && fabsf(y - gCursorY) < 0.5f) return;
    gCursorX = x; gCursorY = y;
    SendMouse(Platform::InputEventType::MouseMove, x, y);
}

void SetLeftButton(int owner, bool down)
{
    const int before = gLeftOwners;
    gLeftOwners = down ? (gLeftOwners | owner) : (gLeftOwners & ~owner);
    if (before == 0 && gLeftOwners != 0) {
        PadLog("PAD: LeftDown @%d,%d", (int)gCursorX, (int)gCursorY);
        SendMouse(Platform::InputEventType::LeftDown, gCursorX, gCursorY);
    } else if (before != 0 && gLeftOwners == 0) {
        PadLog("PAD: LeftUp @%d,%d", (int)gCursorX, (int)gCursorY);
        SendMouse(Platform::InputEventType::LeftUp, gCursorX, gCursorY);
    }
}

void SetRightButton(int owner, bool down)
{
    const int before = gRightOwners;
    gRightOwners = down ? (gRightOwners | owner) : (gRightOwners & ~owner);
    if (before == 0 && gRightOwners != 0) {
        PadLog("PAD: RightDown (cast) @%d,%d", (int)gCursorX, (int)gCursorY);
        SendMouse(Platform::InputEventType::RightDown, gCursorX, gCursorY);
    } else if (before != 0 && gRightOwners == 0) {
        PadLog("PAD: RightUp @%d,%d", (int)gCursorX, (int)gCursorY);
        SendMouse(Platform::InputEventType::RightUp, gCursorX, gCursorY);
    }
}

void PulseKey(int vk, const char* what)
{
    if (gPulseCount >= (int)(sizeof(gPulses) / sizeof(gPulses[0]))) return;
    gPulses[gPulseCount++] = { vk, true };
    Platform::InjectVirtualKey(vk, true);
    PadLog("PAD: key pulse %s (vk 0x%02X)", what, vk);
}

void ReleaseAllInputs()
{
    if (gLeftOwners)  { gLeftOwners = 0;  SendMouse(Platform::InputEventType::LeftUp, gCursorX, gCursorY); }
    if (gRightOwners) { gRightOwners = 0; SendMouse(Platform::InputEventType::RightUp, gCursorX, gCursorY); }
    for (int i = 0; i < gPulseCount; ++i) Platform::InjectVirtualKey(gPulses[i].vk, false);
    gPulseCount = 0;
    if (gBackTabHeld) { Platform::InjectVirtualKey(9, false); gBackTabHeld = false; }
    gBackDown = false;
    gAutoCast = false;
    gTapA.down = gTapY.down = false;
    gUiClickUpPending = false;
    gDriving = false;
}

// Radial deadzone with rescale: returns magnitude 0..1 and unit direction.
float StickVector(Sint16 ax, Sint16 ay, float& dx, float& dy)
{
    float x = ax / 32767.0f, y = ay / 32767.0f;
    float mag = sqrtf(x * x + y * y);
    if (mag < gDeadzone || mag < 0.0001f) { dx = dy = 0.0f; return 0.0f; }
    float scaled = (mag - gDeadzone) / (1.0f - gDeadzone);
    if (scaled > 1.0f) scaled = 1.0f;
    dx = x / mag; dy = y / mag;
    return scaled;
}

// ── Skill selection (mirrors the bottom-bar grid's own writes) ──────────────
// Learned skills: CharacterAttribute[slot + 87] != 0 for slot 0..59.
// Ctrl+N hotkey assignment: CharacterAttribute[(SelectedHero << 6) + slot + 215].
char* SkillCA() { return (char*)CharacterAttribute; }

void SelectSkillSlot(int slot, const char* why)
{
    if (!Hero || !CharacterAttribute || slot < 0 || slot >= 60) return;
    ((CHARACTER*)(uintptr_t)Hero)->CurrentSkill = (BYTE)slot;
    DAT_0055a3e4 = (DWORD)slot;                 // bottom-bar highlight mirror
    PlayBuffer(0x19, nullptr, 0);               // the grid's selection click
    PadLog("PAD: skill select slot %d (type %d) - %s", slot,
           (unsigned char)SkillCA()[slot + 87], why);
}

void CycleSkill(int dir)
{
    if (!Hero || !CharacterAttribute) { PadLog("PAD: skill cycle ignored (no hero)"); return; }
    char* CA = SkillCA();
    const int cur = ((CHARACTER*)(uintptr_t)Hero)->CurrentSkill;
    for (int step = 1; step <= 60; ++step) {
        const int s = ((cur + dir * step) % 60 + 60) % 60;
        if (CA[s + 87] != 0) {
            if (s != cur) SelectSkillSlot(s, dir > 0 ? "RB cycle" : "LB cycle");
            return;
        }
    }
    PadLog("PAD: skill cycle - no learned skills");
}

void QuickSlot(int n)
{
    if (!Hero || !CharacterAttribute) { PadLog("PAD: quick slot %d ignored (no hero)", n); return; }
    char* CA = SkillCA();
    const int row = (int)SelectedHero;
    for (int i = 0; i < 60; ++i)
        if (CA[i + 87] != 0 && (unsigned char)CA[(row << 6) + i + 215] == (unsigned char)n) {
            SelectSkillSlot(i, "quick slot (hotkey assignment)");
            return;
        }
    int seen = 0;
    for (int i = 0; i < 60; ++i)
        if (CA[i + 87] != 0 && ++seen == n) {
            SelectSkillSlot(i, "quick slot (nth learned)");
            return;
        }
    PadLog("PAD: quick slot %d - nothing assigned", n);
}

// LB+RB chord: open the expanded skill list by clicking the bottom-bar grid
// toggle (UI 640x480 point 0x140,0x1CE — the hit area Chat_InputTick tests).
void FireSkillGridClick(float winW, float winH)
{
    MoveCursorTo(0x140 * winW / 640.0f, 0x1CE * winH / 480.0f, true);
    SetLeftButton(kLeftUI, true);
    gUiClickUpPending = true;   // released next tick, like a real click
    PadLog("PAD: LB+RB chord - skill list toggle click");
}

// ── Self-test harness ────────────────────────────────────────────────────────
// Attaches a virtual Xbox-style gamepad and scripts inputs on a timeline.
// The module under test cannot tell it apart from a real pad.
bool SelfTestAttach()
{
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);   // this SDL vintage: version = sizeof(desc)
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = 0x045e;   // Microsoft
    desc.product_id = 0x028e;  // Xbox 360 Controller
    desc.naxes = 6;
    desc.nbuttons = 21;
    desc.nhats = 1;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY) |
                     (1u << SDL_GAMEPAD_AXIS_RIGHTX) | (1u << SDL_GAMEPAD_AXIS_RIGHTY) |
                     (1u << SDL_GAMEPAD_AXIS_LEFT_TRIGGER) | (1u << SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    desc.button_mask = 0x1FFFFFu;   // gamepad buttons 0..20
    desc.name = "MEAMU LabPad (virtual)";
    gVirtId = SDL_AttachVirtualJoystick(&desc);
    if (!gVirtId) { PadLog("PAD-SELFTEST: attach failed: %s", SDL_GetError()); return false; }
    gVirtJoy = SDL_OpenJoystick(gVirtId);
    if (!gVirtJoy) { PadLog("PAD-SELFTEST: open joystick failed: %s", SDL_GetError()); return false; }
    if (!SDL_IsGamepad(gVirtId)) {
        // Give the virtual device the standard Xbox mapping under its own GUID.
        SDL_GUID guid = SDL_GetJoystickGUIDForID(gVirtId);
        char guidStr[64]; SDL_GUIDToString(guid, guidStr, sizeof(guidStr));
        char map[512];
        snprintf(map, sizeof(map),
            "%s,MEAMU LabPad,a:b0,b:b1,x:b2,y:b3,back:b6,guide:b8,start:b7,"
            "leftstick:b9,rightstick:b10,leftshoulder:b4,rightshoulder:b5,"
            "dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,"
            "leftx:a0,lefty:a1,rightx:a3,righty:a4,lefttrigger:a2,righttrigger:a5,"
            "platform:Linux,", guidStr);
        SDL_AddGamepadMapping(map);
        PadLog("PAD-SELFTEST: added gamepad mapping for virtual device");
    }
    PadLog("PAD-SELFTEST: virtual gamepad attached (id %u)", (unsigned)gVirtId);
    return true;
}

void SelfTestDrive()
{
    if (!gVirtJoy) return;
#if !defined(_WIN32)
    // External pause for login drives: while /tmp/pad-drive-pause exists the
    // virtual pad sits neutral so a real (xdotool) mouse can work the UI
    // without the roam legs parking the cursor / holding virtual buttons.
    static bool s_paused = false;
    if (access("/tmp/pad-drive-pause", F_OK) == 0) {
        if (!s_paused) {
            s_paused = true;
            for (int a = 0; a < 6; ++a) SDL_SetJoystickVirtualAxis(gVirtJoy, a, a >= 4 ? -32768 : 0);
            for (int b = 0; b < 21; ++b) SDL_SetJoystickVirtualButton(gVirtJoy, b, false);
            PadLog("PAD-SELFTEST: paused by /tmp/pad-drive-pause (pad neutral)");
        }
        return;
    }
    s_paused = false;
#endif
    if (!gSelfTestT0) {
        // Start the timeline only once the title scene is up — the intro
        // logos + data load make early ticks sparse and would compress it.
        if (SceneFlag >= 2) {
            gSelfTestT0 = SDL_GetTicks();
            PadLog("PAD-SELFTEST: title reached - timeline starts");
        }
        return;
    }
    const double t = (SDL_GetTicks() - gSelfTestT0) / 1000.0;
    int step;
    if      (t < 1.5)  step = 0;   // idle on the title
    else if (t < 3.5)  step = 1;   // right stick RIGHT -> cursor +X
    else if (t < 5.5)  step = 2;   // right stick DOWN  -> cursor +Y
    else if (t < 9.0)  step = 3;   // left stick UP     -> move (virtual LMB held)
    else if (t < 10.0) step = 4;   // release sticks
    else if (t < 11.0) step = 5;   // RT press          -> left click at cursor
    else if (t < 13.0) step = 6;   // LT hold           -> right click (cast)
    else if (t < 14.0) step = 7;   // idle
    else if (t < 14.4) step = 8;   // A press           -> Space (pickup)
    else if (t < 15.5) step = 9;   // idle
    else if (t < 16.0) step = 10;  // D-pad Up          -> Q (HP potion)
    else if (t < 17.0) step = 11;  // idle
    else if (t < 17.5) step = 12;  // RB press          -> skill cycle up
    else if (t < 18.0) step = 13;  // RB released
    else if (t < 18.5) step = 14;  // LB press          -> skill cycle down
    else if (t < 19.0) step = 15;  // LB released
    else if (t < 19.5) step = 16;  // X press           -> quick skill slot 1
    else if (t < 20.0) step = 17;  // X released
    else if (t < 20.5) step = 18;  // Start press       -> Esc (menu)
    else if (t < 21.0) step = 19;  // Start released
    else if (t < 21.5) step = 20;  // D-pad Right       -> W (MP potion)
    else if (t < 22.0) step = 21;  // D-pad Right released
    else if (t < 22.5) step = 22;  // D-pad Down        -> E (SD)
    else if (t < 23.5) step = 23;  // D-pad Down released
    else if (t < 24.5) step = 24;  // detach (hot-unplug release check)
    else               step = 25;  // done
    if (getenv("MU_PAD_SELFTEST_HOLD") && step > 23) step = 23;   // never detach
    if (step == gSelfStep) {
        // Login-drive roam: after the scripted timeline, walk a slow
        // N/E/S/W square so an in-world hero visibly moves each leg.
        if (getenv("MU_PAD_SELFTEST_HOLD") && t >= 26.0 && gVirtJoy) {
            const int leg = (int)((t - 26.0) / 4.0) % 4;
            static int s_lastLeg = -1;
            if (leg != s_lastLeg) {
                s_lastLeg = leg;
                for (int a = 0; a < 6; ++a) SDL_SetJoystickVirtualAxis(gVirtJoy, a, a >= 4 ? -32768 : 0);
                if (leg == 0) { SDL_SetJoystickVirtualAxis(gVirtJoy, 1, -24000); PadLog("PAD-SELFTEST: roam N"); }
                if (leg == 1) { SDL_SetJoystickVirtualAxis(gVirtJoy, 0, 24000);  PadLog("PAD-SELFTEST: roam E"); }
                if (leg == 2) { SDL_SetJoystickVirtualAxis(gVirtJoy, 1, 24000);  PadLog("PAD-SELFTEST: roam S"); }
                if (leg == 3) { SDL_SetJoystickVirtualAxis(gVirtJoy, 0, -24000); PadLog("PAD-SELFTEST: roam W"); }
            }
        }
        return;
    }
    gSelfStep = step;
    if (gPad) PadLog("PAD-SELFTEST: [diag] gamepad axes LX=%d LY=%d RX=%d RY=%d LT=%d RT=%d",
        SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_LEFTX), SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_LEFTY),
        SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_RIGHTX), SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_RIGHTY),
        SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    // NOTE: the virtual device's raw axes follow SDL gamepad order
    // (0=LX 1=LY 2=RX 3=RY 4=LT 5=RT); face/shoulder raw buttons follow the
    // mapping string (A=0 B=1 X=2 Y=3 LB=4 RB=5 Back=6 Start=7); the D-pad
    // reads as raw buttons 11..14 (11=up 12=down 13=left 14=right).
    auto axis = [](int a, Sint16 v) { SDL_SetJoystickVirtualAxis(gVirtJoy, a, v); };
    auto btn  = [](int b, bool d) { SDL_SetJoystickVirtualButton(gVirtJoy, b, d); };
    // Trigger axes (4/5) rest at raw -32768: SDL rescales raw->gamepad as
    // (v+32768)/2 for triggers, so raw 0 would read as half-pressed (16383).
    auto zeroAxes = [&]() { for (int a = 0; a < 6; ++a) axis(a, a >= 4 ? -32768 : 0); };
    switch (step) {
    case 1: PadLog("PAD-SELFTEST: step 1 - right stick RIGHT"); axis(2, 24000); break;
    case 2: PadLog("PAD-SELFTEST: step 2 - right stick DOWN");  axis(2, 0); axis(3, 24000); break;
    case 3: PadLog("PAD-SELFTEST: step 3 - left stick UP (move)"); axis(3, 0); axis(1, -24000); break;
    case 4: PadLog("PAD-SELFTEST: step 4 - sticks released"); zeroAxes(); break;
    case 5: PadLog("PAD-SELFTEST: step 5 - RT press"); axis(5, 32767); break;
    case 6: PadLog("PAD-SELFTEST: step 6 - LT hold"); axis(5, -32768); axis(4, 32767); break;
    case 7: PadLog("PAD-SELFTEST: step 7 - triggers released"); zeroAxes(); break;
    case 8: PadLog("PAD-SELFTEST: step 8 - A press"); btn(0, true); break;
    case 9: PadLog("PAD-SELFTEST: step 9 - A released"); btn(0, false); break;
    case 10: PadLog("PAD-SELFTEST: step 10 - D-pad UP"); btn(11, true); break;
    case 11: PadLog("PAD-SELFTEST: step 11 - D-pad released"); btn(11, false); break;
    case 12: PadLog("PAD-SELFTEST: step 12 - RB press"); btn(5, true); break;
    case 13: PadLog("PAD-SELFTEST: step 13 - RB released"); btn(5, false); break;
    case 14: PadLog("PAD-SELFTEST: step 14 - LB press"); btn(4, true); break;
    case 15: PadLog("PAD-SELFTEST: step 15 - LB released"); btn(4, false); break;
    case 16: PadLog("PAD-SELFTEST: step 16 - X press"); btn(2, true); break;
    case 17: PadLog("PAD-SELFTEST: step 17 - X released"); btn(2, false); break;
    case 18: PadLog("PAD-SELFTEST: step 18 - Start press"); btn(7, true); break;
    case 19: PadLog("PAD-SELFTEST: step 19 - Start released"); btn(7, false); break;
    case 20: PadLog("PAD-SELFTEST: step 20 - D-pad RIGHT"); btn(14, true); break;
    case 21: PadLog("PAD-SELFTEST: step 21 - D-pad released"); btn(14, false); break;
    case 22: PadLog("PAD-SELFTEST: step 22 - D-pad DOWN"); btn(12, true); break;
    case 23: PadLog("PAD-SELFTEST: step 23 - D-pad released"); btn(12, false); break;
    case 24:
        PadLog("PAD-SELFTEST: step 24 - DETACH virtual pad");
        SDL_CloseJoystick(gVirtJoy); gVirtJoy = nullptr;
        SDL_DetachVirtualJoystick(gVirtId); gVirtId = 0;
        break;
    case 25: PadLog("PAD-SELFTEST: timeline complete"); break;
    default: break;
    }
}

// ── Pad adoption (hot-plug by polling) ───────────────────────────────────────
void PollPadPresence()
{
    if (gPad) {
        if (!SDL_GamepadConnected(gPad)) {
            PadLog("PAD: disconnected (id %u) - releasing held inputs", (unsigned)gPadId);
            ReleaseAllInputs();
            SDL_CloseGamepad(gPad);
            gPad = nullptr; gPadId = 0;
        }
        return;
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) return;
    if (count > 0) {
        gPad = SDL_OpenGamepad(ids[0]);
        if (gPad) {
            gPadId = ids[0];
            const char* name = SDL_GetGamepadName(gPad);
            PadLog("PAD: connected '%s' (id %u)", name ? name : "?", (unsigned)gPadId);
            memset(gPrevButtons, 0, sizeof(gPrevButtons));
            gPrevRT = gPrevLT = false;
            gPrevChord = false;
            Rumble(0, 6000, 90);   // connect confirmation buzz
        }
    }
    SDL_free(ids);
}

} // namespace

// Char-select enter driver: with MU_PAD_ENTERWORLD set, once the character
// select scene is up, synthesize a genuine double-click on the character
// name plate through the same DeliverSDLInput path a real double-click
// takes (LeftDown/Up, then the LeftDoubleClick event that sets the game's
// double-click flag). Retries every few seconds until the scene changes.
void SelfTestEnterWorld()
{
    if (SceneFlag != 4) return;
    static Uint64 s_next = 0;
    static int s_stage = 0;
    const Uint64 now = SDL_GetTicks();
    if (now < s_next) return;
    auto emit = [](Platform::InputEventType t, int x, int y) {
        Platform::InputEvent ev{}; ev.type = t; ev.x = x; ev.y = y;
        Platform::DeliverSDLInput(ev);
    };
    switch (s_stage) {
    case 0: emit(Platform::InputEventType::MouseMove, 113, 420); s_stage = 1; s_next = now + 350; break;
    case 1: emit(Platform::InputEventType::LeftDown, 113, 420);  s_stage = 2; s_next = now + 90;  break;
    case 2: emit(Platform::InputEventType::LeftUp, 113, 420);    s_stage = 3; s_next = now + 130; break;
    case 3:
        emit(Platform::InputEventType::LeftDoubleClick, 113, 420);
        emit(Platform::InputEventType::LeftDown, 113, 420);
        PadLog("PAD-SELFTEST: enter-world double-click sent at name plate");
        s_stage = 4; s_next = now + 90; break;
    case 4: emit(Platform::InputEventType::LeftUp, 113, 420);    s_stage = 0; s_next = now + 3000; break;
    }
}

// ── Tap/hold buttons (A = Space/quick-3, Y = Inventory/quick-2) ─────────────
void TapHoldTick(TapHold& th, Uint64 now)
{
    const bool down = SDL_GetGamepadButton(gPad, th.btn);
    if (down && !th.down) {
        th.down = true; th.longFired = false; th.downMs = now;
    } else if (down && th.down && !th.longFired && now - th.downMs >= kLongPressMs) {
        th.longFired = true;
        PadLog("PAD: button hold - quick skill slot %d", th.quickSlot);
        QuickSlot(th.quickSlot);
    } else if (!down && th.down) {
        th.down = false;
        if (!th.longFired) PulseKey(th.tapVk, th.tapWhat);
    }
    gPrevButtons[th.btn] = down;
}


bool Gamepad_IsActive() { return gEnabled && gPad != nullptr; }
bool Gamepad_IsDriving() { return gDriving; }

void Gamepad_RenderIndicator()
{
    if (!Gamepad_IsActive()) return;
    UI_RenderText(578, 6, (char*)"PAD", (LPSIZE)0x0, '\0', 0);
}

void Gamepad_Tick()
{
    if (!gInitTried) {
        gInitTried = true;
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
            PadLog("PAD: SDL_INIT_GAMEPAD failed: %s", SDL_GetError());
        gEnabled    = gUserSettings.GetGamepadEnable();
        gDeadzone   = gUserSettings.GetGamepadDeadzone() / 100.0f;
        gCursorSpd  = (float)gUserSettings.GetGamepadCursorSpeed();
        gSelfTest   = (getenv("MU_PAD_SELFTEST") != nullptr);
        PadLog("PAD: module init (enabled=%d deadzone=%.2f cursor=%.0f px/s%s)",
               gEnabled ? 1 : 0, gDeadzone, gCursorSpd, gSelfTest ? ", self-test" : "");
        gLastMs = SDL_GetTicks();
    }
    if (!gEnabled) { gLastMs = SDL_GetTicks(); return; }

    if (gSelfTest && !gVirtJoy && gSelfStep < 24) SelfTestAttach();
    if (gSelfTest) SelfTestDrive();
    if (gSelfTest && getenv("MU_PAD_ENTERWORLD")) SelfTestEnterWorld();

    // Finish last frame's key pulses (down was delivered last tick).
    for (int i = 0; i < gPulseCount; ++i)
        if (gPulses[i].upPending) { Platform::InjectVirtualKey(gPulses[i].vk, false); gPulses[i].upPending = false; }
    if (gPulseCount && !gPulses[0].upPending) gPulseCount = 0;

    // Finish the chord's skill-grid click (down went out last tick).
    if (gUiClickUpPending) { gUiClickUpPending = false; SetLeftButton(kLeftUI, false); }

    PollPadPresence();
    if (!gPad) { gLastMs = SDL_GetTicks(); gDriving = false; return; }

    const Uint64 now = SDL_GetTicks();
    float dt = (now - gLastMs) / 1000.0f;
    gLastMs = now;
    if (dt > 0.1f) dt = 0.1f;

    const float winW = (float)gWindow.GetRealWidth();
    const float winH = (float)gWindow.GetRealHeight();
    if (!gCursorInit) { gCursorX = winW * 0.5f; gCursorY = winH * 0.5f; gCursorInit = true; }

    // ── Tap/hold face buttons (A / Y) ────────────────────────────────────────
    TapHoldTick(gTapA, now);
    TapHoldTick(gTapY, now);

    // ── Back: tap = M (move list), hold = Tab (minimap) ─────────────────────
    {
        const bool down = SDL_GetGamepadButton(gPad, SDL_GAMEPAD_BUTTON_BACK);
        if (down && !gBackDown) { gBackDown = true; gBackDownMs = now; }
        else if (down && gBackDown && !gBackTabHeld && now - gBackDownMs >= kTabHoldMs) {
            gBackTabHeld = true;
            Platform::InjectVirtualKey(9, true);
            PadLog("PAD: Back hold - Tab/minimap on");
        } else if (!down && gBackDown) {
            gBackDown = false;
            if (gBackTabHeld) {
                gBackTabHeld = false;
                Platform::InjectVirtualKey(9, false);
                PadLog("PAD: Back released - Tab/minimap off");
            } else {
                PulseKey(0x4D, "M/move-list");
            }
        }
        gPrevButtons[SDL_GAMEPAD_BUTTON_BACK] = down;
    }

    // ── Left stick: movement (owns the cursor while deflected) ───────────────
    // Arrow keys take movement precedence while held (either source can take
    // over; the keyboard wins ties so arrows + pad triggers can combo).
    const bool arrowsHeld = NavMove_ArrowsHeld();
    float ldx, ldy;
    const float lmag = StickVector(SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_LEFTX),
                                   SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_LEFTY), ldx, ldy);
    const bool moving = lmag > 0.0f && !arrowsHeld;
    if (moving) {
        const float radius = kMoveRadius * (winW < winH ? winW : winH) * (0.4f + 0.6f * lmag);
        MoveCursorTo(winW * 0.5f + ldx * radius, winH * 0.5f + ldy * radius, false);
        if ((gLeftOwners & kLeftMove) == 0) PadLog("PAD: move start dir=(%.2f,%.2f) mag=%.2f", ldx, ldy, lmag);
        SetLeftButton(kLeftMove, true);
    } else {
        if (gLeftOwners & kLeftMove) PadLog("PAD: move stop");
        SetLeftButton(kLeftMove, false);

        // ── Right stick: free cursor (only when not steering) ────────────────
        float rdx, rdy;
        const float rmag = StickVector(SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_RIGHTX),
                                       SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_RIGHTY), rdx, rdy);
        if (rmag > 0.0f) {
            float nx = gCursorX + rdx * rmag * gCursorSpd * dt;
            float ny = gCursorY + rdy * rmag * gCursorSpd * dt;
            if (nx < 0) nx = 0; if (ny < 0) ny = 0;
            if (nx > winW - 1) nx = winW - 1; if (ny > winH - 1) ny = winH - 1;
            static float s_logAcc = 0;
            s_logAcc += dt;
            if (s_logAcc > 0.5f) { s_logAcc = 0; PadLog("PAD: cursor -> %d,%d", (int)nx, (int)ny); }
            MoveCursorTo(nx, ny, false);
        }
    }

    // ── Triggers ─────────────────────────────────────────────────────────────
    const bool rt = SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerOn;
    if (rt != gPrevRT) { gPrevRT = rt; PadLog(rt ? "PAD: RT down" : "PAD: RT up"); SetLeftButton(kLeftRT, rt); }
    const bool lt = SDL_GetGamepadAxis(gPad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerOn;
    if (lt != gPrevLT) {
        gPrevLT = lt;
        SetRightButton(kRightLT, lt);
        if (lt) Rumble(4000, 0, 50);   // cast-engage thump
    }
    gDriving = moving || rt || lt;

    // ── Bumpers: skill cycling (+ LB+RB chord = skill list) ─────────────────
    const bool lb = SDL_GetGamepadButton(gPad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    const bool rb = SDL_GetGamepadButton(gPad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    if (lb && !gPrevButtons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER])  { PadLog("PAD: button LB down"); CycleSkill(-1); }
    if (rb && !gPrevButtons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER]) { PadLog("PAD: button RB down"); CycleSkill(+1); }
    gPrevButtons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER]  = lb;
    gPrevButtons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = rb;
    const bool chord = lb && rb;
    if (chord && !gPrevChord) FireSkillGridClick(winW, winH);
    gPrevChord = chord;

    // ── Stick clicks: L3 auto-cast latch, R3 cursor recenter ────────────────
    const bool l3 = SDL_GetGamepadButton(gPad, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    if (l3 && !gPrevButtons[SDL_GAMEPAD_BUTTON_LEFT_STICK]) {
        gAutoCast = !gAutoCast;
        SetRightButton(kRightAuto, gAutoCast);
        PadLog(gAutoCast ? "PAD: L3 - auto-cast latch ON" : "PAD: L3 - auto-cast latch OFF");
        Rumble(0, 5000, 60);
    }
    gPrevButtons[SDL_GAMEPAD_BUTTON_LEFT_STICK] = l3;
    const bool r3 = SDL_GetGamepadButton(gPad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    if (r3 && !gPrevButtons[SDL_GAMEPAD_BUTTON_RIGHT_STICK]) {
        MoveCursorTo(winW * 0.5f, winH * 0.5f, true);
        PadLog("PAD: R3 - cursor recentered on hero");
    }
    gPrevButtons[SDL_GAMEPAD_BUTTON_RIGHT_STICK] = r3;

    // ── Simple edge buttons ──────────────────────────────────────────────────
    // B and Start both pulse Esc: in this client Esc is the single back key —
    // it closes the D-menu/chat log, closes the system menu, or opens it.
    static const struct { SDL_GamepadButton b; const char* name; int vk; const char* what; } kBtns[] = {
        { SDL_GAMEPAD_BUTTON_EAST,       "B",      27,   "Esc/back"     },
        { SDL_GAMEPAD_BUTTON_START,      "Start",  27,   "Esc/menu"     },
        { SDL_GAMEPAD_BUTTON_DPAD_UP,    "DUp",    0x51, "Q/HP-potion"  },
        { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "DRight", 0x57, "W/MP-potion"  },
        { SDL_GAMEPAD_BUTTON_DPAD_DOWN,  "DDown",  0x45, "E/SD-potion"  },
        { SDL_GAMEPAD_BUTTON_DPAD_LEFT,  "DLeft",  0x43, "C/character"  },
    };
    for (const auto& kb : kBtns) {
        const bool down = SDL_GetGamepadButton(gPad, kb.b);
        if (down && !gPrevButtons[kb.b]) {
            PadLog("PAD: button %s down", kb.name);
            PulseKey(kb.vk, kb.what);
        }
        gPrevButtons[kb.b] = down;
    }

    // ── X: quick skill slot 1 ────────────────────────────────────────────────
    const bool x = SDL_GetGamepadButton(gPad, SDL_GAMEPAD_BUTTON_WEST);
    if (x && !gPrevButtons[SDL_GAMEPAD_BUTTON_WEST]) {
        PadLog("PAD: button X down");
        QuickSlot(1);
    }
    gPrevButtons[SDL_GAMEPAD_BUTTON_WEST] = x;
}
