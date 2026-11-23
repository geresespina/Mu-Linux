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
//   Skill buttons A = '1' · X = '2' · Y = '3' · B = '4' · RB = '5' · LB = '6'
//                 RT = '7' · LT = '8' · L3 = '9' · R3 = '0' — every one
//                 mirrors its digit key, honestly: key down while held,
//                 key up on release. KbPlay's digit path does the rest:
//                 it selects the button's skill, auto-targets the closest
//                 valid monster in that skill's range and hold-casts
//                 until the key is released (see PADCAST below).
//   D-pad         Up = Q (HP potion) · Right = W (MP) · Down = E (SD) · Left = C
//   Start         scheme toggle (Train 62)      Back = M move list (mirrored)
//
// PADCAST (2026-11-16, G's final layout — a chain of rulings the same
// afternoon): EVERY skill button is a digit mirror into the one shared
// auto-target cast path (KbPlay's digit handling). A held button
// repeat-casts its skill at the closest valid enemy in that skill's
// range, retargeting as targets die; release stops the cast; and pad
// casts NEVER latch the cast-streak AUTO (pad law — KbPlay reads
// Gamepad_PadCastHeld). Retired by these rulings: the RT/LT virtual
// mouse-button mirrors, LB/RB skill cycling, the LB+RB skill-list
// chord, R3's cursor recenter, and L3's unmapped slot.
//
// PADFIX2 (2026-11-16, G's ruling): ONE button = ONE mapped action, mirrored
// honestly — down while held, up when let go. NO tap-vs-hold duals and NO
// long-press alternates anywhere. Removed: the 2026-11-14 tap/hold layer
// (A tap slot / hold Space latch, X hold digit-cast, Y hold Inventory,
// B hold Esc), the Back tap-M vs hold-Tab dual, and the L3 auto-cast latch
// (a sustained virtual RMB not tied to a physical hold). Quick-slot skill
// selection now happens through the game's own digit-key handling, exactly
// as a held keyboard '1'..'4' behaves.
//
// Config.ini [Gamepad]: Enable (default 1), Deadzone (percent, default 25),
// CursorSpeed (px/s, default 900) — Config/UserSettings.
//
// Sandbox proof: MU_PAD_SELFTEST=1 attaches an SDL *virtual* gamepad and
// scripts an input timeline, logging every step + reaction via DbgLogPublic.
// A real pad takes the identical path (SDL abstracts the transport).
//
// PADFIX (2026-11-13) — native Windows pad backends (base path + DI):
// rev.5 read pads ONLY through SDL3's gamepad API. On the target Windows
// machine that path surfaced NOTHING — first a DirectInput Logitech Dual
// Action, then a genuine XInput Xbox 360 controller, both healthy in
// Windows, both invisible in game (no PAD indicator, no input). Every
// client-side suspect checks out clean (Gamepad_Tick is called every frame,
// Config defaults Enable=1, SDL loads XInput1_4.dll first, the module IS
// compiled into the shipped exe) — the break is inside SDL's Windows
// joystick enumeration for this build/machine, a layer this module cannot
// see into. Fix: stop depending on it on Windows. Every frame the active
// backend fills ONE unified state (DIPadState, SDL semantics —
// Input/GamepadDI.h) and the mapping layer reads only that state:
//   1. SDL gamepad (unchanged; the only backend off Windows, and still
//      first choice where it works — its values are copied verbatim).
//   2. Native XInput (Windows): xinput1_4.dll -> xinput9_1_0.dll ->
//      xinput1_3.dll, loaded dynamically; all 4 slots polled every frame,
//      so hot-plug is inherent. Also gives the Xbox pad rumble.
//   3. Native DirectInput8 (Windows): for non-XInput pads (Dual Action).
// XInput beats DirectInput because XInput devices also appear in DI
// enumeration; either native backend yields to an SDL gamepad. While no
// backend has a pad, PollPadPresence logs (throttled) how many joysticks /
// gamepads SDL sees, so the next diagnosis starts from data, not guesses.

#include "stdafx.h"
#include "Input/Gamepad.h"
#include "Input/GamepadDI.h"     // DIPadState / DIPadTranslate (padfix)
#include "Input/NavMove.h"       // NavMove_ArrowsHeld (movement yield)
#include "Render/ActionCamera.h"  // Train 48: scheme + pointer lock + OnMouseDelta
#include "Input/TargetLock.h"     // Train 64: pad-cast auto-lock
#include "Input/ActionMove.h"     // Train 48: ActionMove_PadDash
#include "Input/Fishing.h"        // Train 56: fishing verb
#include "Input/PadMap.h"         // Train 100: the account pad map drives the sheet
#include "Platform/IInput.h"     // Platform::InputEvent / InputEventType
#include "Platform/NativeBridge.h" // Platform::NativeWindow/NativeInstance (DI coop level)
#include "Core/Window.h"         // gWindow (real pixel size)
#include "Config/UserSettings.h" // gUserSettings ([Gamepad] section)
#include "Entity/EntityTypes.h"  // CHARACTER (CurrentSkill)
#include <SDL3/SDL.h>
#if defined(_WIN32)
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>              // IDirectInput8 / DIJOYSTATE2 (padfix backend)
#pragma comment(lib, "dinput8")
#pragma comment(lib, "dxguid")
#endif
#include <cctype>
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

// The unified pad state is indexed with SDL_GamepadButton / SDL_GamepadAxis
// values by the mapping code below; the DIPad* slots must mirror them.
static_assert((int)DIPAD_BTN_SOUTH == (int)SDL_GAMEPAD_BUTTON_SOUTH, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_EAST == (int)SDL_GAMEPAD_BUTTON_EAST, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_WEST == (int)SDL_GAMEPAD_BUTTON_WEST, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_NORTH == (int)SDL_GAMEPAD_BUTTON_NORTH, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_BACK == (int)SDL_GAMEPAD_BUTTON_BACK, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_START == (int)SDL_GAMEPAD_BUTTON_START, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_LEFT_STICK == (int)SDL_GAMEPAD_BUTTON_LEFT_STICK, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_RIGHT_STICK == (int)SDL_GAMEPAD_BUTTON_RIGHT_STICK, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_LEFT_SHOULDER == (int)SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_RIGHT_SHOULDER == (int)SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_DPAD_UP == (int)SDL_GAMEPAD_BUTTON_DPAD_UP, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_DPAD_DOWN == (int)SDL_GAMEPAD_BUTTON_DPAD_DOWN, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_DPAD_LEFT == (int)SDL_GAMEPAD_BUTTON_DPAD_LEFT, "DIPad/SDL button drift");
static_assert((int)DIPAD_BTN_DPAD_RIGHT == (int)SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "DIPad/SDL button drift");
static_assert((int)DIPAD_AXIS_LEFTX == (int)SDL_GAMEPAD_AXIS_LEFTX, "DIPad/SDL axis drift");
static_assert((int)DIPAD_AXIS_LEFTY == (int)SDL_GAMEPAD_AXIS_LEFTY, "DIPad/SDL axis drift");
static_assert((int)DIPAD_AXIS_RIGHTX == (int)SDL_GAMEPAD_AXIS_RIGHTX, "DIPad/SDL axis drift");
static_assert((int)DIPAD_AXIS_RIGHTY == (int)SDL_GAMEPAD_AXIS_RIGHTY, "DIPad/SDL axis drift");
static_assert((int)DIPAD_AXIS_LEFT_TRIGGER == (int)SDL_GAMEPAD_AXIS_LEFT_TRIGGER, "DIPad/SDL axis drift");
static_assert((int)DIPAD_AXIS_RIGHT_TRIGGER == (int)SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, "DIPad/SDL axis drift");
static_assert(DIPAD_BUTTON_SLOTS >= SDL_GAMEPAD_BUTTON_COUNT, "DIPad button slots too few");

namespace {

// ── Tuning (MAPPING.md; deadzone + cursor speed come from Config.ini) ───────
constexpr int   kTriggerOn   = 16000;    // of 32767
constexpr float kMoveRadius  = 0.22f;    // of min(winW, winH)

// ── State ────────────────────────────────────────────────────────────────────
SDL_Gamepad*   gPad        = nullptr;
SDL_JoystickID gPadId      = 0;
bool           gInitTried  = false;
bool           gEnabled    = true;       // Config.ini [Gamepad] Enable
float          gDeadzone   = 0.25f;
float          gCursorSpd  = 900.0f;
Uint64         gLastMs     = 0;
bool           gDriving    = false;      // stick-move active this frame

float gCursorX = -1.0f, gCursorY = -1.0f;   // window px; <0 = center on first use
bool  gCursorInit = false;

// The virtual left button has a single owner — stick-move — kept as an
// owner mask so the resync/watchdog logic below keeps its shape. The
// virtual right button has NO pad owner any more (PADCAST, 2026-11-16):
// the triggers are digit mirrors now, and KbPlay's digit path owns the
// synthetic right button for pad casts, exactly as for keyboard digits.
enum LeftOwner { kLeftMove = 1 };
int  gLeftOwners = 0;

// One-frame key pulses: injected "down" on the press frame, "up" next tick.
struct KeyPulse { int vk; bool upPending; };
KeyPulse gPulses[8];
int      gPulseCount = 0;

// Mirrored buttons (PADFIX2 + PADCAST): one button = one key, down while
// the button is held, up when it is released — no tap/hold split, no
// long-press layer. The ten skill buttons mirror the skill shortcut
// digit keys (G's final layout, 2026-11-16): A=1 X=2 Y=3 B=4 RB=5 LB=6
// RT=7 LT=8 L3=9 R3=0. Back mirrors M. The two triggers are axis-driven,
// so they keep their own edge trackers (gPrevRT/gPrevLT) instead of a
// gPrevButtons slot.
struct MirrorKey { SDL_GamepadButton btn; const char* name; int vk; };
const MirrorKey kSkillKeys[] = {
    { SDL_GAMEPAD_BUTTON_SOUTH,         "A",  0x31 },   // skill 1
    { SDL_GAMEPAD_BUTTON_WEST,          "X",  0x32 },   // skill 2
    { SDL_GAMEPAD_BUTTON_NORTH,         "Y",  0x33 },   // skill 3
    { SDL_GAMEPAD_BUTTON_EAST,          "B",  0x34 },   // skill 4
    { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "RB", 0x35 },   // skill 5
    { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  "LB", 0x36 },   // skill 6
    { SDL_GAMEPAD_BUTTON_LEFT_STICK,     "L3", 0x39 },   // skill 9
    { SDL_GAMEPAD_BUTTON_RIGHT_STICK,    "R3", 0x30 },   // skill 0
};
const MirrorKey kBackKey{ SDL_GAMEPAD_BUTTON_BACK, "Back", 0x4D };   // M

bool gPrevButtons[SDL_GAMEPAD_BUTTON_COUNT] = {};
bool gPrevRT = false, gPrevLT = false;

// ── Unified pad state (padfix) ───────────────────────────────────────────────
// Whichever backend is active (SDL gamepad or DirectInput) refills this once
// per frame in CapturePadState(); the mapping logic reads ONLY this state,
// so both backends drive identical MAPPING.md semantics.
DIPadState gState;
bool       gStateValid = false;

bool PadButton(int b) { return gStateValid && b >= 0 && b < DIPAD_BUTTON_SLOTS && gState.buttons[b]; }
short PadAxis(int a) { return (gStateValid && a >= 0 && a < DIPAD_AXIS_SLOTS) ? gState.axes[a] : 0; }

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

#if defined(_WIN32)
bool XInputPresent();                                // XInput backend, below
void XInputRumble(Uint16 low, Uint16 high, Uint32 ms);
#endif

void Rumble(Uint16 low, Uint16 high, Uint32 ms)
{
    if (gPad) { SDL_RumbleGamepad(gPad, low, high, ms); return; }
#if defined(_WIN32)
    if (XInputPresent()) { XInputRumble(low, high, ms); return; }
#endif
    // DirectInput backend: no rumble — driving the Dual Action's motors
    // would need FF effect objects; cosmetic only, out of padfix scope.
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

// (PADCAST: SetRightButton is gone — no pad path synthesizes the right
// mouse button any more. Pad casts run through KbPlay's digit path,
// which owns its own synthetic right button.)

void PulseKey(int vk, const char* what)
{
    if (gPulseCount >= (int)(sizeof(gPulses) / sizeof(gPulses[0]))) return;
    gPulses[gPulseCount++] = { vk, true };
    Platform::InjectVirtualKey(vk, true);
    PadLog("PAD: key pulse %s (vk 0x%02X)", what, vk);
}

} // (Train 84 wrapper lives below the anonymous namespace)

void PulseKeyShared(int vk, const char* what) { PulseKey(vk, what); }

namespace {
void ReleaseAllInputs()
{
    if (gLeftOwners) { gLeftOwners = 0; SendMouse(Platform::InputEventType::LeftUp, gCursorX, gCursorY); }
    for (int i = 0; i < gPulseCount; ++i) Platform::InjectVirtualKey(gPulses[i].vk, false);
    gPulseCount = 0;
    // Mirrored keys: send the balancing key-up for any still tracked as
    // down — every skill button (button-kind via its gPrevButtons slot,
    // the two triggers via their own trackers) plus Back.
    for (const auto& mk : kSkillKeys)
        if (gPrevButtons[mk.btn]) Platform::InjectVirtualKey(mk.vk, false);
    if (gPrevRT) Platform::InjectVirtualKey(0x37, false);   // RT — skill 7
    if (gPrevLT) Platform::InjectVirtualKey(0x38, false);   // LT — skill 8
    if (gPrevButtons[kBackKey.btn]) Platform::InjectVirtualKey(kBackKey.vk, false);
    // PADFIX2: reset every edge tracker, so a still-held trigger/button
    // re-fires its down edge next tick instead of being silently swallowed
    // (edge trackers previously survived a release-all, desyncing mirrors).
    memset(gPrevButtons, 0, sizeof(gPrevButtons));
    gPrevRT = gPrevLT = false;
    gDriving = false;
}

// True while the pad holds ANY virtual input down (mouse owner, skill
// mirror, trigger mirror, Back). The resync paths in the tick key off
// this — not just the mouse owners — so a state loss or scene change can
// never leave a mirrored digit key wedged down (PADCAST).
bool AnyVirtualHeld()
{
    if (gLeftOwners != 0) return true;
    for (const auto& mk : kSkillKeys)
        if (gPrevButtons[mk.btn]) return true;
    if (gPrevRT || gPrevLT) return true;
    return gPrevButtons[kBackKey.btn];
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

// (PADCAST, 2026-11-16: the pad-side skill selection that lived here —
// SelectSkillSlot/CycleSkill for LB/RB cycling and the LB+RB chord's
// skill-grid click — is retired. The bumpers are digit mirrors now;
// skill selection for every pad skill button happens in KbPlay's digit
// path, exactly as it does for a physical keyboard digit.)

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
    else if (t < 11.0) step = 5;   // RT press          -> digit '7' mirror (skill 7)
    else if (t < 13.0) step = 6;   // LT hold           -> digit '8' mirror (skill 8 cast)
    else if (t < 14.0) step = 7;   // idle
    else if (t < 14.4) step = 8;   // A press (held)    -> Space latch (hold layer)
    else if (t < 15.5) step = 9;   // idle
    else if (t < 16.0) step = 10;  // D-pad Up          -> Q (HP potion)
    else if (t < 17.0) step = 11;  // idle
    else if (t < 17.5) step = 12;  // RB press          -> digit '5' mirror (skill 5)
    else if (t < 18.0) step = 13;  // RB released
    else if (t < 18.5) step = 14;  // LB press          -> digit '6' mirror (skill 6)
    else if (t < 19.0) step = 15;  // LB released
    else if (t < 19.75) step = 16; // X press (tap)     -> quick skill slot 2 on release
    else if (t < 20.0) step = 17;  // X released
    else if (t < 20.5) step = 18;  // Start press       -> Esc (menu)
    else if (t < 21.0) step = 19;  // Start released
    else if (t < 21.5) step = 20;  // D-pad Right       -> W (MP potion)
    else if (t < 22.0) step = 21;  // D-pad Right released
    else if (t < 22.5) step = 22;  // D-pad Down        -> E (SD)
    else if (t < 23.5) step = 23;  // D-pad Down released
    else if (t < 23.75) step = 26; // A press (tap test)
    else if (t < 24.5) step = 27;  // A released (250 ms tap) -> quick slot 1
    else if (t < 25.5) step = 28;  // A press (hold test)
    else if (t < 26.0) step = 29;  // A released (hold) -> Space up, no slot
    else if (t < 26.25) step = 30; // X press (tap test)
    else if (t < 27.0) step = 31;  // X released (250 ms tap) -> quick slot 2
    else if (t < 28.0) step = 32;  // X press (hold test) -> digit '2' held
    else if (t < 28.5) step = 33;  // X released (hold) -> digit '2' up
    else if (t < 28.75) step = 34; // Y press (tap test)
    else if (t < 29.5) step = 35;  // Y released (250 ms tap) -> quick slot 3
    else if (t < 30.5) step = 36;  // Y press (hold test) -> Inventory pulse
    else if (t < 31.0) step = 37;  // Y released (hold)
    else if (t < 31.25) step = 38; // B press (tap test)
    else if (t < 32.0) step = 39;  // B released (250 ms tap) -> quick slot 4
    else if (t < 33.0) step = 40;  // B press (hold test) -> Esc pulse
    else if (t < 33.5) step = 41;  // B released (hold)
    else if (t < 34.5) step = 42;  // detach (hot-unplug release check)
    else               step = 43;  // done
    if (getenv("MU_PAD_SELFTEST_HOLD") && step > 41) step = 41;   // never detach
    if (step == gSelfStep) {
        // Login-drive roam: after the scripted timeline, walk a slow
        // N/E/S/W square so an in-world hero visibly moves each leg.
        if (getenv("MU_PAD_SELFTEST_HOLD") && t >= 36.0 && gVirtJoy) {
            const int leg = (int)((t - 36.0) / 4.0) % 4;
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
        PadAxis(SDL_GAMEPAD_AXIS_LEFTX), PadAxis(SDL_GAMEPAD_AXIS_LEFTY),
        PadAxis(SDL_GAMEPAD_AXIS_RIGHTX), PadAxis(SDL_GAMEPAD_AXIS_RIGHTY),
        PadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), PadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
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
    case 16: PadLog("PAD-SELFTEST: step 16 - X press (tap)"); btn(2, true); break;
    case 17: PadLog("PAD-SELFTEST: step 17 - X released (tap - quick slot 2)"); btn(2, false); break;
    case 18: PadLog("PAD-SELFTEST: step 18 - Start press"); btn(7, true); break;
    case 19: PadLog("PAD-SELFTEST: step 19 - Start released"); btn(7, false); break;
    case 20: PadLog("PAD-SELFTEST: step 20 - D-pad RIGHT"); btn(14, true); break;
    case 21: PadLog("PAD-SELFTEST: step 21 - D-pad released"); btn(14, false); break;
    case 22: PadLog("PAD-SELFTEST: step 22 - D-pad DOWN"); btn(12, true); break;
    case 23: PadLog("PAD-SELFTEST: step 23 - D-pad released"); btn(12, false); break;
    case 26: PadLog("PAD-SELFTEST: step 26 - A press (tap test)"); btn(0, true); break;
    case 27: PadLog("PAD-SELFTEST: step 27 - A released (tap - expect quick slot 1, no Space)"); btn(0, false); break;
    case 28: PadLog("PAD-SELFTEST: step 28 - A press (hold test)"); btn(0, true); break;
    case 29: PadLog("PAD-SELFTEST: step 29 - A released (hold - expect Space latch only, no quick slot)"); btn(0, false); break;
    case 30: PadLog("PAD-SELFTEST: step 30 - X press (tap test)"); btn(2, true); break;
    case 31: PadLog("PAD-SELFTEST: step 31 - X released (tap - expect quick slot 2)"); btn(2, false); break;
    case 32: PadLog("PAD-SELFTEST: step 32 - X press (hold test - expect digit 2 held)"); btn(2, true); break;
    case 33: PadLog("PAD-SELFTEST: step 33 - X released (hold - expect digit 2 up, no quick slot)"); btn(2, false); break;
    case 34: PadLog("PAD-SELFTEST: step 34 - Y press (tap test)"); btn(3, true); break;
    case 35: PadLog("PAD-SELFTEST: step 35 - Y released (tap - expect quick slot 3)"); btn(3, false); break;
    case 36: PadLog("PAD-SELFTEST: step 36 - Y press (hold test - expect Inventory pulse)"); btn(3, true); break;
    case 37: PadLog("PAD-SELFTEST: step 37 - Y released (hold - no quick slot)"); btn(3, false); break;
    case 38: PadLog("PAD-SELFTEST: step 38 - B press (tap test)"); btn(1, true); break;
    case 39: PadLog("PAD-SELFTEST: step 39 - B released (tap - expect quick slot 4, no Esc)"); btn(1, false); break;
    case 40: PadLog("PAD-SELFTEST: step 40 - B press (hold test - expect Esc pulse)"); btn(1, true); break;
    case 41: PadLog("PAD-SELFTEST: step 41 - B released (hold - no quick slot)"); btn(1, false); break;
    case 42:
        PadLog("PAD-SELFTEST: step 42 - DETACH virtual pad");
        SDL_CloseJoystick(gVirtJoy); gVirtJoy = nullptr;
        SDL_DetachVirtualJoystick(gVirtId); gVirtId = 0;
        break;
    case 43: PadLog("PAD-SELFTEST: timeline complete"); break;
    default: break;
    }
}

// ── DirectInput backend (Windows only; padfix) ──────────────────────────────
// Enumerates native DI8 game controllers and reads DIJOYSTATE2 each frame,
// translating into the unified state (GamepadDI.h). Adopted ONLY while no
// SDL gamepad is present; re-enumerates every 2 s while idle (hot-plug);
// released the moment an SDL gamepad appears (SDL/XInput keeps priority,
// exactly as before the fix).
#if defined(_WIN32)
IDirectInput8*       gDI = nullptr;
IDirectInputDevice8* gDIDev = nullptr;
DIPadRawState        gDIRawProto;        // per-axis presence + ranges, read at adopt
char                 gDIName[128] = {};
int                  gDIFailStreak = 0;  // consecutive failed poll/acquire rounds
Uint64               gDINextScanMs = 0;  // next enumeration attempt

struct DIFoundDevice { bool found = false; GUID guid{}; char name[128] = {}; };

// XInput pads also surface through DirectInput enumeration; the SDL path
// owns them, so never adopt one here (belt and braces — adoption only runs
// while SDL reports no gamepad at all).
bool DINameLooksXInput(const char* name)
{
    auto contains = [](const char* hay, const char* needle) {
        const size_t nl = strlen(needle);
        for (const char* p = hay; *p; ++p) {
            size_t i = 0;
            while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) ++i;
            if (i == nl) return true;
        }
        return false;
    };
    return contains(name, "xbox") || contains(name, "xinput");
}

BOOL CALLBACK DIEnumDeviceCb(const DIDEVICEINSTANCE* inst, VOID* ctx)
{
    DIFoundDevice* out = (DIFoundDevice*)ctx;
    if (out->found) return DIENUM_STOP;
    // The client builds MultiByte (_MBCS), so tszProductName is char[].
    char name[128] = {};
    strncpy_s(name, inst->tszProductName, sizeof(name) - 1);
    if (DINameLooksXInput(name)) return DIENUM_CONTINUE;
    out->found = true;
    out->guid = inst->guidInstance;
    strncpy_s(out->name, name, sizeof(out->name) - 1);
    return DIENUM_STOP;
}

BOOL CALLBACK DIEnumAxisCb(const DIDEVICEOBJECTINSTANCE* inst, VOID* ctx)
{
    IDirectInputDevice8* dev = (IDirectInputDevice8*)ctx;
    int slot = -1;
    if (inst->guidType == GUID_XAxis) slot = DIRA_X;
    else if (inst->guidType == GUID_YAxis) slot = DIRA_Y;
    else if (inst->guidType == GUID_ZAxis) slot = DIRA_Z;
    else if (inst->guidType == GUID_RxAxis) slot = DIRA_RX;
    else if (inst->guidType == GUID_RyAxis) slot = DIRA_RY;
    else if (inst->guidType == GUID_RzAxis) slot = DIRA_RZ;
    if (slot < 0) return DIENUM_CONTINUE;
    gDIRawProto.axisPresent[slot] = true;
    DIPROPRANGE range{};
    range.diph.dwSize = sizeof(DIPROPRANGE);
    range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    range.diph.dwHow = DIPH_BYOFFSET;
    range.diph.dwObj = inst->dwOfs;
    if (SUCCEEDED(dev->GetProperty(DIPROP_RANGE, &range.diph))) {
        gDIRawProto.axisMin[slot] = range.lMin;
        gDIRawProto.axisMax[slot] = range.lMax;
    }
    return DIENUM_CONTINUE;
}

void DIPadRelease()
{
    if (gDIDev) { gDIDev->Unacquire(); gDIDev->Release(); gDIDev = nullptr; }
    gDIName[0] = '\0';
    gDIFailStreak = 0;
}

bool DIPadAdopt()
{
    if (!gDI) {
        HINSTANCE hinst = Platform::NativeInstance();
        if (!hinst) hinst = GetModuleHandleW(nullptr);
        if (FAILED(DirectInput8Create(hinst, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&gDI, nullptr))) {
            PadLog("PAD-DI: DirectInput8Create failed");
            return false;
        }
    }
    DIFoundDevice found;
    if (FAILED(gDI->EnumDevices(DI8DEVCLASS_GAMECTRL, DIEnumDeviceCb, &found, DIEDFL_ATTACHEDONLY)) || !found.found)
        return false;   // nothing attached right now — caller schedules the rescan

    IDirectInputDevice8* dev = nullptr;
    if (FAILED(gDI->CreateDevice(found.guid, &dev, nullptr)) || !dev) return false;
    if (FAILED(dev->SetDataFormat(&c_dfDIJoystick2))) { dev->Release(); return false; }
    HWND hwnd = Platform::NativeWindow();
    if (!hwnd) hwnd = GetForegroundWindow();
    if (hwnd && FAILED(dev->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
        PadLog("PAD-DI: SetCooperativeLevel failed (continuing; foreground acquire may still work)");
    gDIRawProto = DIPadRawState{};
    dev->EnumObjects(DIEnumAxisCb, dev, DIDFT_AXIS);
    if (FAILED(dev->Acquire()))
        PadLog("PAD-DI: initial Acquire failed (retried per-frame in capture)");
    gDIDev = dev;
    gDIFailStreak = 0;
    strncpy_s(gDIName, found.name, sizeof(gDIName) - 1);
    PadLog("PAD-DI: connected '%s' (DirectInput fallback)", gDIName);
    memset(gPrevButtons, 0, sizeof(gPrevButtons));
    gPrevRT = gPrevLT = false;
    return true;
}

// One frame of DirectInput state into the unified state. False = no fresh
// state this frame (transient loss keeps last state semantics via
// gStateValid going false, i.e. all inputs read as released-edge-safe).
bool DIPadCapture()
{
    if (!gDIDev) return false;
    HRESULT hr = gDIDev->Poll();
    if (FAILED(hr)) {
        hr = gDIDev->Acquire();
        if (FAILED(hr)) {
            // Sustained acquire failure = the device is gone (unplugged);
            // drop it cleanly and let PollPadPresence rescan on its timer.
            if (++gDIFailStreak >= 30) {
                PadLog("PAD-DI: disconnected ('%s') - releasing held inputs", gDIName);
                ReleaseAllInputs();
                DIPadRelease();
                gDINextScanMs = SDL_GetTicks() + 2000;
            }
            return false;
        }
    }
    DIJOYSTATE2 js{};
    hr = gDIDev->GetDeviceState(sizeof(js), &js);
    if (FAILED(hr)) {
        gDIDev->Acquire();   // input lost mid-frame; retry next frame
        // PADFIX2: this path previously never counted toward the release
        // streak, so a device whose state reads kept failing wedged the
        // module with gStateValid=false forever — and any virtual button
        // held at that moment was NEVER released (hero walked forever).
        if (++gDIFailStreak >= 30) {
            PadLog("PAD-DI: state reads keep failing ('%s') - releasing held inputs", gDIName);
            ReleaseAllInputs();
            DIPadRelease();
            gDINextScanMs = SDL_GetTicks() + 2000;
        }
        return false;
    }
    gDIFailStreak = 0;
    DIPadRawState raw = gDIRawProto;
    raw.axis[DIRA_X] = js.lX;   raw.axis[DIRA_Y] = js.lY;   raw.axis[DIRA_Z] = js.lZ;
    raw.axis[DIRA_RX] = js.lRx; raw.axis[DIRA_RY] = js.lRy; raw.axis[DIRA_RZ] = js.lRz;
    for (int i = 0; i < 4; ++i) raw.pov[i] = js.rgdwPOV[i];
    memcpy(raw.buttons, js.rgbButtons, sizeof(raw.buttons));
    DIPadTranslate(raw, gState);
    return true;
}
#endif // _WIN32

bool DIPadPresent()
{
#if defined(_WIN32)
    return gDIDev != nullptr;
#else
    return false;
#endif
}

// ── XInput backend (Windows only; padfix base-path fix) ─────────────────────
// Native XInput polling, independent of SDL's Windows joystick stack. The
// ABI structs are declared locally (stable since 2005) and the DLL is
// loaded dynamically in the order xinput1_4.dll (ships with Windows 8+) ->
// xinput9_1_0.dll (ships with Vista+) -> xinput1_3.dll (legacy DirectX
// redist — NOT part of Windows; last resort only). All four slots are
// polled every frame: hot-plug and unplug need no enumeration state.
#if defined(_WIN32)
struct XIGamepadABI { WORD wButtons; BYTE bLeftTrigger; BYTE bRightTrigger; SHORT sThumbLX; SHORT sThumbLY; SHORT sThumbRX; SHORT sThumbRY; };
struct XIStateABI { DWORD dwPacketNumber; XIGamepadABI Gamepad; };
struct XIVibrationABI { WORD wLeftMotorSpeed; WORD wRightMotorSpeed; };
struct XICapsABI { BYTE Type; BYTE SubType; WORD Flags; XIGamepadABI Gamepad; XIVibrationABI Vibration; };
using XInputGetStateFn = DWORD (WINAPI *)(DWORD, XIStateABI*);
using XInputSetStateFn = DWORD (WINAPI *)(DWORD, XIVibrationABI*);
using XInputGetCapsFn = DWORD (WINAPI *)(DWORD, DWORD, XICapsABI*);

HMODULE           gXIDll = nullptr;
XInputGetStateFn  gXIGetState = nullptr;
XInputSetStateFn  gXISetState = nullptr;
XInputGetCapsFn   gXIGetCaps = nullptr;
bool              gXILoadTried = false;
int               gXISlot = -1;
char              gXIDllName[32] = {};
bool              gXIRumbleActive = false;
Uint64            gXIRumbleStopMs = 0;

bool XInputLoad()
{
    if (gXILoadTried) return gXIGetState != nullptr;
    gXILoadTried = true;
    static const char* kDlls[] = { "xinput1_4.dll", "xinput9_1_0.dll", "xinput1_3.dll" };
    for (const char* name : kDlls) {
        HMODULE dll = LoadLibraryA(name);
        if (!dll) continue;
        auto getState = (XInputGetStateFn)GetProcAddress(dll, "XInputGetState");
        if (!getState) { FreeLibrary(dll); continue; }
        gXIDll = dll;
        gXIGetState = getState;
        gXISetState = (XInputSetStateFn)GetProcAddress(dll, "XInputSetState");
        gXIGetCaps = (XInputGetCapsFn)GetProcAddress(dll, "XInputGetCapabilities");
        strncpy_s(gXIDllName, name, sizeof(gXIDllName) - 1);
        PadLog("PAD-XI: XInput loaded from %s", name);
        return true;
    }
    PadLog("PAD-XI: no XInput DLL found (tried xinput1_4 / xinput9_1_0 / xinput1_3)");
    return false;
}

void XInputRumbleStop()
{
    if (gXIRumbleActive && gXISlot >= 0 && gXISetState) {
        XIVibrationABI vib{ 0, 0 };
        gXISetState(gXISlot, &vib);
    }
    gXIRumbleActive = false;
}

void XInputRumble(Uint16 low, Uint16 high, Uint32 ms)
{
    if (gXISlot < 0 || !gXISetState) return;
    XIVibrationABI vib{ low, high };
    if (gXISetState(gXISlot, &vib) == ERROR_SUCCESS) {
        gXIRumbleActive = true;
        gXIRumbleStopMs = SDL_GetTicks() + ms;
    }
}

// One frame of XInput state into the unified state; also the adoption scan
// (runs every frame while no XInput device is active). False = no XInput
// device this frame.
bool XInputCapture()
{
    if (!XInputLoad()) return false;
    if (gXISlot < 0) {
        for (int slot = 0; slot < 4; ++slot) {
            XIStateABI st{};
            if (gXIGetState(slot, &st) != ERROR_SUCCESS) continue;
            if (gXIGetCaps) {
                XICapsABI caps{};
                // SubType 1 = gamepad; 0 = unknown (accept). Skip wheels,
                // guitars, dance pads and other non-gamepad subtypes.
                if (gXIGetCaps(slot, 1 /*XINPUT_FLAG_GAMEPAD*/, &caps) == ERROR_SUCCESS &&
                    caps.SubType != 1 && caps.SubType != 0)
                    continue;
            }
            gXISlot = slot;
            memset(gPrevButtons, 0, sizeof(gPrevButtons));
            gPrevRT = gPrevLT = false;
            PadLog("PAD-XI: connected slot %d (native XInput via %s)", slot, gXIDllName);
            XInputRumble(0, 6000, 90);   // connect confirmation buzz, as on the SDL path
            break;
        }
        if (gXISlot < 0) return false;
    }
    XIStateABI st{};
    if (gXIGetState(gXISlot, &st) != ERROR_SUCCESS) {
        PadLog("PAD-XI: disconnected (slot %d) - releasing held inputs", gXISlot);
        ReleaseAllInputs();
        XInputRumbleStop();
        gXISlot = -1;
        return false;
    }
    if (gXIRumbleActive && SDL_GetTicks() >= gXIRumbleStopMs) XInputRumbleStop();
    XIPadRawState raw;
    raw.thumbLX = st.Gamepad.sThumbLX; raw.thumbLY = st.Gamepad.sThumbLY;
    raw.thumbRX = st.Gamepad.sThumbRX; raw.thumbRY = st.Gamepad.sThumbRY;
    raw.leftTrigger = st.Gamepad.bLeftTrigger; raw.rightTrigger = st.Gamepad.bRightTrigger;
    raw.buttons = st.Gamepad.wButtons;
    XIPadTranslate(raw, gState);
    return true;
}
#endif // _WIN32

bool XInputPresent()
{
#if defined(_WIN32)
    return gXISlot >= 0;
#else
    return false;
#endif
}

bool NativePadPresent()
{
    return DIPadPresent() || XInputPresent();
}

// Refill the unified state from the active backend (called once per frame).
// Priority: SDL gamepad -> native XInput -> native DirectInput.
void CapturePadState()
{
    gState = DIPadState{};
    gStateValid = false;
    if (gPad) {
        for (int a = 0; a < DIPAD_AXIS_SLOTS; ++a)
            gState.axes[a] = SDL_GetGamepadAxis(gPad, (SDL_GamepadAxis)a);
        const int nb = SDL_GAMEPAD_BUTTON_COUNT < DIPAD_BUTTON_SLOTS
            ? SDL_GAMEPAD_BUTTON_COUNT : DIPAD_BUTTON_SLOTS;
        for (int b = 0; b < nb; ++b)
            gState.buttons[b] = SDL_GetGamepadButton(gPad, (SDL_GamepadButton)b);
        gStateValid = true;
        return;
    }
#if defined(_WIN32)
    if (XInputCapture()) {
        if (gDIDev) {
            PadLog("PAD-XI: XInput device active - releasing DirectInput device '%s'", gDIName);
            ReleaseAllInputs();
            DIPadRelease();
        }
        gStateValid = true;
        return;
    }
    if (gDIDev) gStateValid = DIPadCapture();
#endif
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
        if (gPad) return;
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids) {
        if (count > 0) {
            gPad = SDL_OpenGamepad(ids[0]);
            if (gPad) {
                gPadId = ids[0];
                const char* name = SDL_GetGamepadName(gPad);
                PadLog("PAD: connected '%s' (id %u)", name ? name : "?", (unsigned)gPadId);
                memset(gPrevButtons, 0, sizeof(gPrevButtons));
                gPrevRT = gPrevLT = false;
                Rumble(0, 6000, 90);   // connect confirmation buzz
            }
        }
        SDL_free(ids);
    }
#if defined(_WIN32)
    if (gPad) {
        if (gDIDev) {
            PadLog("PAD-DI: SDL gamepad present - releasing DirectInput device '%s'", gDIName);
            ReleaseAllInputs();
            DIPadRelease();
        }
        return;
    }
    if (gDIDev) return;   // DI device active; DIPadCapture health-checks it
    const Uint64 nowMs = SDL_GetTicks();
    if (!XInputPresent() && nowMs >= gDINextScanMs) {
        if (!DIPadAdopt()) gDINextScanMs = nowMs + 2000;   // hot-plug rescan every 2 s
    }
#endif
    // Diagnostic (padfix): while no backend has a pad, report what SDL
    // sees, throttled to 5 s — joysticks>0 with gamepads=0 means SDL
    // detection works but no mapping matched; both 0 means SDL's joystick
    // stack sees nothing at all. Lands in debug.log via PadLog.
    if (!gPad && !NativePadPresent()) {
        static Uint64 s_lastWaitLog = 0;
        const Uint64 nowMs2 = SDL_GetTicks();
        if (nowMs2 - s_lastWaitLog >= 5000) {
            s_lastWaitLog = nowMs2;
            int nj = 0, ng = 0;
            SDL_JoystickID* jids = SDL_GetJoysticks(&nj);
            if (jids) SDL_free(jids);
            SDL_JoystickID* gids = SDL_GetGamepads(&ng);
            if (gids) SDL_free(gids);
            PadLog("PAD: waiting for pad (SDL sees %d joystick(s), %d gamepad(s))", nj, ng);
        }
    }
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

// ── Mirrored buttons (PADFIX2 + PADCAST) ───────────────────────────────────
// One button = one key, mirrored honestly: the virtual key goes down on the
// press edge and up on the release edge — exactly what the physical button
// does. No duration is ever measured, so nothing can fire a second,
// different action for holding a button longer. Used for the eight
// button-kind skill buttons (A/X/Y/B/RB/LB/L3/R3) and for Back.
void MirrorKeyTick(const MirrorKey& mk)
{
    const bool down = PadButton(mk.btn);
    if (down != gPrevButtons[mk.btn]) {
        Platform::InjectVirtualKey(mk.vk, down);
        PadLog("PAD: button %s %s - key 0x%02X %s", mk.name,
               down ? "down" : "up", mk.vk, down ? "held" : "released");
    }
    gPrevButtons[mk.btn] = down;
}

// ── Trigger mirrors (PADCAST) ──────────────────────────────────────────────
// RT/LT are analog axes, not buttons: the mirror edge is the threshold
// crossing, tracked by the trigger's own prev flag — the same trackers
// ReleaseAllInputs balances and resets, so a held trigger is covered by
// every PADFIX2 release path and re-edges cleanly after a release-all.
void TriggerKeyTick(const char* name, int axis, int vk, bool& prev)
{
    const bool down = PadAxis(axis) > kTriggerOn;
    if (down != prev) {
        Platform::InjectVirtualKey(vk, down);
        PadLog("PAD: trigger %s %s - key 0x%02X %s", name,
               down ? "down" : "up", vk, down ? "held" : "released");
    }
    prev = down;
}


bool Gamepad_IsActive() { return gEnabled && (gPad != nullptr || NativePadPresent()); }
bool Gamepad_IsDriving() { return gDriving; }

// True while any of the ten skill buttons is held (button-kind via its
// gPrevButtons slot, triggers via their own trackers). KbPlay's
// cast-streak latch reads this: a cast observed while a pad skill button
// drives it is excluded from the AUTO latch (pad law, 2026-11-16),
// exactly like a cast made while the physical right mouse button is held.
bool gActionPadCastHeld = false;   // Train 48: set by ActionPadTick

bool Gamepad_PadCastHeld()
{
    if (gActionCamera.SchemeIsAction()) return gActionPadCastHeld;
    for (const auto& mk : kSkillKeys)
        if (gPrevButtons[mk.btn]) return true;
    return gPrevRT || gPrevLT;
}

void Gamepad_RenderIndicator()
{
    if (!Gamepad_IsActive()) return;
    UI_RenderText(578, 6, (char*)"PAD", (LPSIZE)0x0, '\0', 0);
}

// ── ACTION-scheme pad map ────────────────────────────────────────────
// Train 81 sheet (G, 2026-11-20): THE TRIGGERS ARE SWAPPED --
// RT HOLD = lock + camera follow + Skill 2 cast-hold + the
// hunt (Train 72's triple verb, ex-LT) · LT = Teleport
// TARGETING (hold to aim, release to land, ex-RT) · X Skill
// 3 · Y Skill 4 · B Skill 5 · A Skill 6 · View Skill 7 · L3
// Skill 8 · D-up Skill 9 · D-down Skill 10 · LB Target Lock ·
// RB Dash · R3 Blink (Space mirror) · D-Left Fishing · Start =
// scheme toggle · left stick = WASD (ActionMove drives,
// camera-relative) · right stick = aim (the teleport marker
// while LT is held). Classic: PADCAST, untouched. (An L3-toggle
// swap was built and CANCELLED by G the same morning -- "Start
// is better" -- nothing moved.)
struct ActPrev {
    bool rb=false, x=false, y=false, view=false, dup=false, ddown=false;
    bool l3=false, r3=false, rt=false, lt=false;
    bool a=false, b=false, lb=false, start=false;
    int  wasdMask=0;
};
ActPrev gActPrev;

void TpTargetCancel(const char* why);

void ActionPadReleaseAll()
{
    const int vks[] = { 0x57, 0x53, 0x41, 0x44, 0x20,
                        0x31, 0x32, 0x33, 0x34, 0x35,
                        0x36, 0x37, 0x38, 0x39, 0x30 };
    for (int vk : vks) Platform::InjectVirtualKey(vk, false);
    gActPrev = ActPrev{};
    gActionPadCastHeld = false;
    TpTargetCancel("scheme flip");
}

// ── Train 59: TELEPORT TARGETING (Action scheme, LT hold since
// Train 81's trigger swap; RT before it) ─────────────────────────────
// G's spec (2026-11-20): LT is Skill 1 = Teleport. HOLDING LT raises
// a landing marker at the spot a tap-blink would take him (facing
// ray, max range); the RIGHT STICK walks the marker tile to tile;
// RELEASING RT teleports to the marker -- through the very same
// ActionMove_TeleportToPoint the double-click blink uses (mode 2
// wire, Chebyshev clamp 8, DriveGates). The marker is honest: gold
// while the tile is landable, RED while it is a wall / out of range
// / the hero's own tile, and releasing on red fires NOTHING.
// Cancels (no packet, marker dies): scheme flip (ActionPadReleaseAll
// above runs on the way out; the Gamepad_Tick state-loss branch
// cancels too), pad state loss, hero death, chat/UI opening mid-hold.
// Keyboard Space blink and Classic RT are untouched.
extern unsigned char* TerrainWall;   // world wall grid (TargetLock's)
struct TpTargetState {
    bool     active = false;
    float    fx = 0.0f, fy = 0.0f;   // marker position, tile space
    int      tx = 0, ty = 0;         // marker tile
    bool     valid = false;
    unsigned lastFxMs = 0;
    int      lastHx = 0, lastHy = 0; // Train 66: hero tile at last anchor
};
TpTargetState gTpT;

static bool TpTileLandable(int hx, int hy, int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx > 255 || ty > 255) return false;
    const int adx = tx > hx ? tx - hx : hx - tx;
    const int ady = ty > hy ? ty - hy : hy - ty;
    const int cheb = adx > ady ? adx : ady;
    if (cheb < 1 || cheb > 8) return false;
    if (!TerrainWall) return false;
    const BYTE wall = TerrainWall[((ty & 0xFF) << 8) | (tx & 0xFF)];
    return (wall & 0x0C) == 0;   // 0x04|0x08 = blocked, as TargetLock reads it
}

// MEAMU Train 85 (G's rule, 2026-11-20): "The marker should only
// ever sit on spots I can ACTUALLY teleport to." The aim itself is
// constrained now: resolve the wanted tile to the NEAREST landable
// tile inside the blink ring -- nearest by Chebyshev ring around
// the wanted tile, then by Manhattan distance, every candidate
// passing the same TpTileLandable test the fire path trusts. The
// marker can therefore never rest on a wall / blocked /
// unstandable tile, and release always lands.
static bool TpResolveLandable(int hx, int hy, int wx, int wy, int& outX, int& outY)
{
    if (TpTileLandable(hx, hy, wx, wy)) { outX = wx; outY = wy; return true; }
    int bestScore = 0x7FFFFFFF;
    bool found = false;
    for (int cx = wx - 8; cx <= wx + 8; cx++) {
        for (int cy = wy - 8; cy <= wy + 8; cy++) {
            if (!TpTileLandable(hx, hy, cx, cy)) continue;
            const int adx = cx > wx ? cx - wx : wx - cx;
            const int ady = cy > wy ? cy - wy : wy - cy;
            const int cheb = adx > ady ? adx : ady;
            const int score = cheb * 100 + adx + ady;
            if (score < bestScore) { bestScore = score; outX = cx; outY = cy; found = true; }
        }
    }
    return found;
}

// Apply Train 85 to the shared marker state: resolve (wx,wy) and,
// when the resolution moved the tile, pull the float anchor onto
// the legal tile so stick/carry math continues FROM the legal
// spot -- the marker slides along a wall's edge instead of
// burying its anchor inside the wall (no dead zone on pull-back).
static void TpApplyLandable(int hx, int hy, int wx, int wy)
{
    int rx = 0, ry = 0;
    if (TpResolveLandable(hx, hy, wx, wy, rx, ry)) {
        if (rx != gTpT.tx || ry != gTpT.ty) { gTpT.fx = (float)rx; gTpT.fy = (float)ry; }
        gTpT.tx = rx; gTpT.ty = ry;
        gTpT.valid = true;
    } else {
        // Nothing landable in the whole blink ring (the hero is
        // walled in): keep the last tile and its verdict.
        gTpT.tx = wx; gTpT.ty = wy;
        gTpT.valid = TpTileLandable(hx, hy, wx, wy);
    }
}

static void TpMarkerFx(CHARACTER* hero)
{
    float pos[3] = { (float)(gTpT.tx * 100 + 50), (float)(gTpT.ty * 100 + 50),
                     hero->Object.Position[2] + 30.0f };
    float ang[3] = { 0.0f, 0.0f, 0.0f };
    float gold[3] = { 1.0f, 0.75f, 0.20f };
    float red[3]  = { 1.0f, 0.15f, 0.15f };
    CreateEffect(1176, pos, ang, gTpT.valid ? gold : red,
                 nullptr, nullptr, (float*)(uintptr_t)0xFFFFFFFF, nullptr, 0);
    gTpT.lastFxMs = (unsigned)SDL_GetTicks();
}

void TpTargetBegin()
{
    CHARACTER* hero = (CHARACTER*)(uintptr_t)Hero;
    if (!hero) return;
    const int hx = (int)hero->PositionX, hy = (int)hero->PositionY;
    const float yawDeg = gActionCamera.PointerLocked()
        ? gActionCamera.GetYaw() : hero->Object.Angle[2];
    const float rad = yawDeg * (3.14159265f / 180.0f);
    gTpT.fx = (float)hx + sinf(rad) * 8.0f;
    gTpT.fy = (float)hy + cosf(rad) * 8.0f;
    TpApplyLandable(hx, hy, (int)lroundf(gTpT.fx), (int)lroundf(gTpT.fy)); // Train 85
    gTpT.lastHx = hx; gTpT.lastHy = hy;   // Train 66: anchor
    gTpT.active = true;
    TpMarkerFx(hero);
    PadLog("PAD: teleport targeting - marker at (%d,%d)", gTpT.tx, gTpT.ty);
}

void TpTargetCancel(const char* why)
{
    if (!gTpT.active) return;
    gTpT.active = false;
    PadLog("PAD: teleport targeting cancelled (%s)", why);
}

// Per-tick while the targeting trigger is held (LT on the pad
// since Train 81; Space on the keyboard since Train 82).
// Returns false when targeting died (caller then treats the
// coming release as a no-op). kbdAim (Train 82): no stick --
// the marker rides the camera aim at full blink range, which
// is where Begin anchored it; mouse-look IS the aim.
bool TpTargetUpdate(float dt, bool kbdAim)
{
    CHARACTER* hero = (CHARACTER*)(uintptr_t)Hero;
    if (!hero || hero->Dead || InputEnable != 0 || DAT_083a7c24 != 0) {
        TpTargetCancel("gates closed");
        return false;
    }
    const int hx = (int)hero->PositionX, hy = (int)hero->PositionY;
    // MEAMU TRAIN 66 (G's refinement, 2026-11-20): "The tele marker
    // should move with me if I hold RT and walk." The marker is
    // anchored RELATIVE to the hero: every tick it is carried by
    // the hero's tile delta, keeping its exact fractional offset,
    // so walking (or a dash) travels the marker -- and its range
    // ring -- with him. The stick walk below then adjusts the
    // offset from wherever the carry left it; release fires at
    // the marker's current spot as before.
    const int dhx = hx - gTpT.lastHx, dhy = hy - gTpT.lastHy;
    if (dhx != 0 || dhy != 0) {
        gTpT.fx += (float)dhx;
        gTpT.fy += (float)dhy;
        gTpT.lastHx = hx;
        gTpT.lastHy = hy;
    }
    if (kbdAim) {
        const float yawDeg = gActionCamera.PointerLocked()
            ? gActionCamera.GetYaw() : hero->Object.Angle[2];
        const float rad = yawDeg * (3.14159265f / 180.0f);
        gTpT.fx = (float)hx + sinf(rad) * 8.0f;
        gTpT.fy = (float)hy + cosf(rad) * 8.0f;
    }
    float rdx, rdy;
    const float rmag = kbdAim ? 0.0f : StickVector(PadAxis(SDL_GAMEPAD_AXIS_RIGHTX),
                                   PadAxis(SDL_GAMEPAD_AXIS_RIGHTY), rdx, rdy);
    if (rmag > 0.0f) {
        const float yawDeg = gActionCamera.PointerLocked()
            ? gActionCamera.GetYaw() : hero->Object.Angle[2];
        const float rad = yawDeg * (3.14159265f / 180.0f);
        const float fxv = sinf(rad), fyv = cosf(rad);       // screen-up on the ground
        // G's play verdict (2026-11-20): stick left/right walked the
        // marker BACKWARDS -- the screen-right vector is negated so
        // the stick's X axis drives the marker the other way. The
        // up/down terms (-fxv*rdy, -fyv*rdy) are byte-identical:
        // vertical was right and stays right.
        const float rxv = fyv, ryv = -fxv;                  // screen-right (X flipped)
        const float kSpeed = 17.0f;                          // tiles / second
        gTpT.fx += (rxv * rdx - fxv * rdy) * kSpeed * dt;
        gTpT.fy += (ryv * rdx - fyv * rdy) * kSpeed * dt;
        // Keep the marker inside the blink ring around the hero's
        // CURRENT tile (same Chebyshev scaling TeleportToPoint uses).
        const float dx = gTpT.fx - (float)hx, dy = gTpT.fy - (float)hy;
        const float adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        const float cheb = adx > ady ? adx : ady;
        if (cheb > 8.0f) {
            const float s = 8.0f / cheb;
            gTpT.fx = (float)hx + dx * s;
            gTpT.fy = (float)hy + dy * s;
        }
    }
    const int ntx = (int)lroundf(gTpT.fx), nty = (int)lroundf(gTpT.fy);
    const int ptx = gTpT.tx, pty = gTpT.ty;
    TpApplyLandable(hx, hy, ntx, nty); // Train 85: marker rests only on landable tiles
    const bool tileChanged = (gTpT.tx != ptx || gTpT.ty != pty);
    if (tileChanged || (unsigned)SDL_GetTicks() - gTpT.lastFxMs >= 110)
        TpMarkerFx(hero);
    return true;
}

bool TpTargetActive() { return gTpT.active; }

void TpTargetFire()
{
    if (!gTpT.active) return;
    gTpT.active = false;
    if (gTpT.valid) {
        PadLog("PAD: teleport to marker (%d,%d)", gTpT.tx, gTpT.ty);
        ActionMove_TeleportToPoint(gTpT.tx, gTpT.ty);
    } else {
        PadLog("PAD: teleport release on invalid marker - no fire");
    }
}

void Meamu_PickUpNearestItem(void);   // Train 94: Player_InputTick's nearest-item fetch

void ActMirror(int vk, bool down, bool& prev)
{
    if (down != prev) { Platform::InjectVirtualKey(vk, down); prev = down; }
}

// ── MEAMU Train 89: landing buffer for pad skill presses ──────────────
// A skill button pressed DURING a teleport round-trip used to be
// eaten: the press landed inside the flight and never cast. Now the
// latest such press is buffered and fired by the 1C landing handler
// (PadBuffer_FlushTeleport): if the button is still held, the mirror
// takes it over as a hold; if it was already released, the flush
// fires a tap and the next pad tick releases it.
static int            gBufVk = 0;
static bool*          gBufPrev = nullptr;
static bool           gBufTapOff = false;

static void ActMirrorBuffered(int vk, bool down, bool& prev)
{
    if (Combat_TeleportInFlight()) {
        if (down) { gBufVk = vk; gBufPrev = &prev; }   // latest press wins
        return;   // swallowed while the flight is open (prev untouched)
    }
    ActMirror(vk, down, prev);
}

void PadBuffer_FlushTeleport()
{
    if (gBufVk == 0 || gBufPrev == nullptr) return;
    const int vk = gBufVk;
    bool* prev = gBufPrev;
    gBufVk = 0; gBufPrev = nullptr;
    Platform::InjectVirtualKey(vk, true);
    *prev = true;
    // If the physical button is already up, the next PadButtonsTick
    // turns this into a tap (releases it); if it is still held, the
    // buffered mirror call next tick sees prev == down and holds.
    gBufTapOff = true;
    PadLog("PAD: buffered cast fired at landing (vk 0x%02X)", vk);
}

// MEAMU Train 64 (G's ruling, 2026-11-20): ONE PAD MAP, ALWAYS.
// This button tick now runs in BOTH schemes — the Action sheet IS
// the pad sheet: RT teleport targeting · LT 2 · X 3 · Y 4 · B 5 ·
// A Dash · RB 6 · R3 Blink · View 7 · L3 8 · D-up 9 · D-down 10 ·
// LB lock · D-Left fishing · Start scheme toggle. What differs by
// scheme is only the STICKS (Action: WASD + aim; Classic: cursor
// steering) and LB's lock flavour (Action: the verb; Classic: a
// Tab pulse cycling the native Tab lock — the verb is Action-only
// by Train 48 design). Classic's PADCAST bindings retired here:
// D-pad potions (Q/W/E), C (character) and Back=M (move list) lose
// their pad buttons; the keyboard keeps every one of them.
void PadButtonsTick(unsigned now, float dt, bool actionPad)
{
    (void)now;
    // MEAMU Train 100 (2026-11-21): THE ACCOUNT MAP DRIVES THE
    // SHEET. Every handler below reads the state of the CONTROL
    // its action is bound to in the active pad map (PadMap --
    // the player's saved account map once it lands, else the
    // Default preset, which IS tonight's layout). With the
    // Default, each handler receives exactly the control it
    // used to read directly, in the same order -- behaviour is
    // unchanged by construction. ONE PAD MAP, ALWAYS (Train 64)
    // still holds: both schemes run this tick; what differs by
    // scheme stays inside the lock and dash handlers, as before.
    // Train 64: snapshot the cast state for the auto-lock edge.
    bool ctl[PC_COUNT];
    ctl[PC_LT] = PadAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerOn;
    ctl[PC_RT] = PadAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerOn;
    ctl[PC_LB] = PadButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    ctl[PC_RB] = PadButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    ctl[PC_L3] = PadButton(SDL_GAMEPAD_BUTTON_LEFT_STICK);
    ctl[PC_R3] = PadButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    ctl[PC_A] = PadButton(SDL_GAMEPAD_BUTTON_SOUTH);
    ctl[PC_B] = PadButton(SDL_GAMEPAD_BUTTON_EAST);
    ctl[PC_X] = PadButton(SDL_GAMEPAD_BUTTON_WEST);
    ctl[PC_Y] = PadButton(SDL_GAMEPAD_BUTTON_NORTH);
    ctl[PC_BACK] = PadButton(SDL_GAMEPAD_BUTTON_BACK);
    ctl[PC_START] = PadButton(SDL_GAMEPAD_BUTTON_START);
    ctl[PC_DUP] = PadButton(SDL_GAMEPAD_BUTTON_DPAD_UP);
    ctl[PC_DDOWN] = PadButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    ctl[PC_DLEFT] = PadButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    ctl[PC_DRIGHT] = PadButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    const auto held = [&ctl](PadAction a) -> bool {
        const int c = PadMap_ControlFor(a);
        return c >= 0 && ctl[c];
    };

    const bool prevCastHeld = gActionPadCastHeld;
    // Lock + pour (Train 72's triple verb: auto-target + camera
    // follow + SKILL 2 CAST-HOLD, one combined hold -- the hold
    // IS the action; release stops the cast, ends the follow and
    // clears the lock, Train 86). Train 81 put it on RT; the map
    // now decides where it lives.
    static bool s_PourPrev = false;
    {
        const bool now_ = held(PA_LOCK_POUR);
        const bool was = s_PourPrev;
        ActMirror(0x32, now_, s_PourPrev);   // Skill 2 cast-hold mirror
        TargetLock_LtHold(now_);             // lock + follow + pour, in-range only (Train 88)
        if (was && !now_) TargetLock_ReleaseUnlock();   // Train 86
    }
    // Skills 1..10 as plain press mirrors (the Train 57/81 sheet
    // carried 3..10 on fixed buttons; the map now places any of
    // the ten on any button). Skill 2 bound as a plain button
    // casts like any other; the pour above is its own action.
    static bool s_SkillPrev[10] = {};
    {
        static const int kVk[10] = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30 };
        bool any = s_PourPrev;
        for (int i = 0; i < 10; i++) {
            ActMirrorBuffered(kVk[i], held((PadAction)(PA_SKILL1 + i)), s_SkillPrev[i]);
            any = any || s_SkillPrev[i];
        }
        gActionPadCastHeld = any;
    }
    // Train 64: PAD-CAST AUTO-LOCK -- a skill press with an empty
    // lock fills it with the closest monster BEFORE KbPlay resolves
    // the cast (the pad tick runs ahead of the sim step that reads
    // the mirrored key). A held manual lock is never overridden:
    // AutoLockClosest itself refuses while any lock stands.
    if (gActionPadCastHeld && !prevCastHeld && KbPlay_SelectedSkillTargeted()) {
        // Train 70: the auto-lock belongs to TARGETED skills only
        // (G's scope ruling) -- a self/summon cast no longer
        // manufactures a lock it cannot use.
        if (TargetLock_AutoLockClosest()) PadLog("PAD: cast auto-lock -> closest monster");
        if (!actionPad) Rumble(4000, 0, 50);   // Classic cast-engage thump (kept from the PADCAST era)
    }

    {   // Teleport targeting (Train 59; hold to aim, release to
        // land -- Train 81 put it on LT; the map decides now).
        static bool s_TpPrev = false;
        const bool rt = held(PA_TELEPORT);
        if (rt && !s_TpPrev) {
            if (InputEnable == 0 && DAT_083a7c24 == 0) TpTargetBegin();
        } else if (rt && gTpT.active) {
            TpTargetUpdate(dt, false);
        } else if (!rt && s_TpPrev) {
            TpTargetFire();
        }
        s_TpPrev = rt;
    }

    // Pick up items (Train 94 put it on R3; the map decides
    // now). One press fetches the nearest ground item; pressing
    // again while fetching steps to the next nearest.
    {
        static bool s_PickPrev = false;
        const bool r3 = held(PA_PICKUP);
        if (r3 && !s_PickPrev) Meamu_PickUpNearestItem();
        s_PickPrev = r3;
    }

    {   // Lock on/off (Train 75 put it on LB; the map decides
        // now): press with a lock standing releases it, press
        // with none engages. NOT a cycle, on any door. Action:
        // the verb lock toggles in TargetLock. Classic: the plain
        // lock toggles the same way -- standing lock clears,
        // empty lock acquires the CLOSEST attackable target
        // (Train 97: every door, both schemes, one fill).
        static bool s_LockPrev = false;
        const bool lb = held(PA_LOCK_TOGGLE);
        if (lb && !s_LockPrev) {
            if (actionPad) {
                TargetLock_Toggle();
                PadLog("PAD: lock toggle (mapped)");
            } else if (KbPlay_LockIndex() >= 0) {
                KbPlay_LockClear();
                PadLog("PAD: lock OFF (Classic, mapped)");
            } else {
                const int best = TargetLock_ClosestAttackable(false);
                if (best >= 0) {
                    KbPlay_LockSet(best);
                    PadLog("PAD: lock on closest (Classic, mapped)");
                } else {
                    PadLog("PAD: no attackable target in range (Classic, mapped)");
                }
            }
        }
        s_LockPrev = lb;
    }

    {   // Fishing verb (Train 56 put it on D-pad Left; the map
        // decides now): cast / reel.
        static bool s_FishPrev = false;
        const bool dl = held(PA_FISHING);
        if (dl && !s_FishPrev) { Fishing_Verb(); PadLog("PAD: fishing verb (mapped)"); }
        s_FishPrev = dl;
    }

    {   // Dash (Train 90 put it on A; the map decides now). Both
        // schemes -- it is a BUTTON, so it lives here, not in the
        // sticks tick. In Action the left stick aims the dash; in
        // Classic there is no WASD stick, so the dash takes the
        // hero's facing.
        static bool s_DashPrev = false;
        const bool aBtn = held(PA_DASH);
        if (aBtn && !s_DashPrev) {
            if (actionPad) {
                float ldx, ldy;
                StickVector(PadAxis(SDL_GAMEPAD_AXIS_LEFTX),
                            PadAxis(SDL_GAMEPAD_AXIS_LEFTY), ldx, ldy);
                const int sx = (ldx >  0.38f) ? 1 : (ldx < -0.38f) ? -1 : 0;
                const int sy = (ldy >  0.38f) ? 1 : (ldy < -0.38f) ? -1 : 0;
                ActionMove_PadDashDir(sx, sy);
            }
            ActionMove_PadDash();
        }
        s_DashPrev = aBtn;
    }

    {   // Scheme toggle (Train 62 put it on Start; the map
        // decides now).
        static bool s_SchemePrev = false;
        // MEAMU Train 111 (2026-11-23): HOLD to flip (500 ms). A tap
        // in a brawl flipped G's scheme 6 times in one 5-minute
        // session (2026-11-22 client log) -- each flip kills WASD
        // steering until he flips back. The keyboard grave key in
        // ActionMove stays an instant flip; only the pad is held.
        static unsigned s_SchemePressT = 0;
        static bool s_SchemeFired = false;
        const bool st = held(PA_SCHEME_TOGGLE);
        const unsigned nowT = (unsigned)SDL_GetTicks();
        if (st && !s_SchemePrev) { s_SchemePressT = nowT; s_SchemeFired = false; }
        if (st && !s_SchemeFired && (nowT - s_SchemePressT) >= 500) {
            PulseKey(0xC0, "scheme toggle");
            s_SchemeFired = true;
        }
        s_SchemePrev = st;
    }
}

void ActionSticksTick(float dt)
{
    // Left stick -> WASD virtual keys (ActionMove owns the drive).
    float ldx, ldy;
    const float lmag = StickVector(PadAxis(SDL_GAMEPAD_AXIS_LEFTX),
                                   PadAxis(SDL_GAMEPAD_AXIS_LEFTY), ldx, ldy);
    int sx = 0, sy = 0;
    if (lmag > 0.0f) {
        sx = (ldx >  0.38f) ? 1 : (ldx < -0.38f) ? -1 : 0;
        sy = (ldy >  0.38f) ? 1 : (ldy < -0.38f) ? -1 : 0;
    }
    const int mask = (sy < 0 ? 1 : 0) | (sy > 0 ? 2 : 0) | (sx < 0 ? 4 : 0) | (sx > 0 ? 8 : 0);
    const int vks[4] = { 0x57, 0x53, 0x41, 0x44 };   // W S A D
    for (int i = 0; i < 4; ++i) {
        const bool want = ((mask >> i) & 1) != 0;
        const bool had  = ((gActPrev.wasdMask >> i) & 1) != 0;
        if (want != had) Platform::InjectVirtualKey(vks[i], want);
    }
    gActPrev.wasdMask = mask;
    gDriving = false;   // the WASD keys drive; the cursor-steer yield stays off

    // Right stick -> aim while pointer-locked; cursor while UI is open.
    // Train 59: while teleport targeting is up, TpTargetUpdate owns
    // the stick (it steers the landing marker) -- aim/cursor yield.
    float rdx, rdy;
    const float rmag = StickVector(PadAxis(SDL_GAMEPAD_AXIS_RIGHTX),
                                   PadAxis(SDL_GAMEPAD_AXIS_RIGHTY), rdx, rdy);
    if (rmag > 0.0f && !gTpT.active) {
        if (gActionCamera.PointerLocked()) {
            gActionCamera.OnMouseDelta((int)(rdx * rmag * 1500.0f * dt),
                                       (int)(rdy * rmag * 1500.0f * dt));
        } else {
            const float winW = (float)gWindow.GetRealWidth();
            const float winH = (float)gWindow.GetRealHeight();
            float nx = gCursorX + rdx * rmag * gCursorSpd * dt;
            float ny = gCursorY + rdy * rmag * gCursorSpd * dt;
            if (nx < 0) nx = 0; if (ny < 0) ny = 0;
            if (nx > winW - 1) nx = winW - 1; if (ny > winH - 1) ny = winH - 1;
            MoveCursorTo(nx, ny, false);
        }
    }
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

    if (gSelfTest && !gVirtJoy && gSelfStep < 42) SelfTestAttach();
    if (gSelfTest) SelfTestDrive();
    if (gSelfTest && getenv("MU_PAD_ENTERWORLD")) SelfTestEnterWorld();

    // Finish last frame's key pulses (down was delivered last tick).
    for (int i = 0; i < gPulseCount; ++i)
        if (gPulses[i].upPending) { Platform::InjectVirtualKey(gPulses[i].vk, false); gPulses[i].upPending = false; }
    if (gPulseCount && !gPulses[0].upPending) gPulseCount = 0;

    // PADFIX2: scene transitions resync the virtual buttons. The game zeroes
    // its click flags on scene/world entry (Game_EnterWorldTick,
    // Player_ProcessInput's scene-change block) without telling this module;
    // a hold carried across the transition would diverge from the game
    // permanently — the owner mask only emits edges, so a mask that never
    // returns to zero never produces a fresh LeftDown again (dead stick on
    // the new scene), and a game-side hold the pad already dropped would
    // never be balanced. Drop our holds at the same moment the game drops
    // its flags; the player's next press is a clean edge on both sides.
    {
        static int s_lastScene = -1;
        const int scene = (int)SceneFlag;
        if (s_lastScene != -1 && scene != s_lastScene && AnyVirtualHeld()) {
            PadLog("PAD: scene change %d->%d while inputs held - releasing all (resync)", s_lastScene, scene);
            ReleaseAllInputs();
        }
        s_lastScene = scene;
    }

    PollPadPresence();
    CapturePadState();
    if (!gStateValid) {
        // PADFIX2: a backend that cannot report state must never keep a
        // virtual input held. Previously this early return skipped every
        // owner release, so a capture failure while moving wedged the
        // virtual LMB down forever — the hero kept re-pathing to the parked
        // cursor and never stopped. Release instead; when state recovers,
        // a still-deflected stick / still-held button or trigger re-fires
        // its down edge and play resumes. PADCAST: the release condition
        // is AnyVirtualHeld(), not just the mouse owners — a mirrored
        // digit key held across a state loss would wedge exactly the
        // same way the LMB did.
        if (AnyVirtualHeld()) {
            PadLog("PAD: pad state lost while inputs held - releasing all");
            TpTargetCancel("pad state lost");
            ReleaseAllInputs();
        }
        gLastMs = SDL_GetTicks(); gDriving = false; return;
    }

    const Uint64 now = SDL_GetTicks();
    float dt = (now - gLastMs) / 1000.0f;
    gLastMs = now;
    if (dt > 0.1f) dt = 0.1f;

    const float winW = (float)gWindow.GetRealWidth();
    const float winH = (float)gWindow.GetRealHeight();
    if (!gCursorInit) { gCursorX = winW * 0.5f; gCursorY = winH * 0.5f; gCursorInit = true; }

    // ── Skill buttons + triggers + Back: digit/M mirrors (PADCAST) ──────────
    // Every skill button feeds KbPlay's digit path, which selects
    // the button's skill and hold-casts until the key is released.
    // (Train 64 adds ONE piece of pad targeting of its own: the
    // cast-press auto-lock in PadButtonsTick.)
    // Train 48: in the Action scheme the pad runs G's Action map;
    // the PADCAST sections below run only in Classic.
    // Train 64: ONE PAD MAP, ALWAYS — PadButtonsTick (the Action
    // sheet) runs in BOTH schemes; only the sticks differ. A scheme
    // flip releases every mirrored input + any in-flight teleport
    // targeting first, then the unified tick re-engages from the
    // physical pad state.
    static bool s_wasActionPad = false;
    const bool actionPad = gActionCamera.SchemeIsAction();
    if (actionPad != s_wasActionPad) ActionPadReleaseAll();
    s_wasActionPad = actionPad;
    PadButtonsTick(now, dt, actionPad);
    if (actionPad) {
        ActionSticksTick(dt);
    } else {

    // ── Left stick: movement (owns the cursor while deflected) ───────────────
    // Arrow keys take movement precedence while held (either source can take
    // over; the keyboard wins ties so arrows + pad triggers can combo).
    const bool arrowsHeld = NavMove_ArrowsHeld();
    float ldx, ldy;
    const float lmag = StickVector(PadAxis(SDL_GAMEPAD_AXIS_LEFTX),
                                   PadAxis(SDL_GAMEPAD_AXIS_LEFTY), ldx, ldy);
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
        const float rmag = StickVector(PadAxis(SDL_GAMEPAD_AXIS_RIGHTX),
                                       PadAxis(SDL_GAMEPAD_AXIS_RIGHTY), rdx, rdy);
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

    // PADCAST: only stick-move "drives" now. The skill buttons are digit
    // mirrors; like a keyboard digit hold they must NOT make KbPlay's
    // cast yield — the yield exists for when the pad steers the cursor
    // with the left stick and holds the left button itself. (Bumpers and
    // stick clicks need no tick code of their own any more: LB/RB/L3/R3
    // are rows in kSkillKeys, ticked by MirrorKeyTick above. The LB+RB
    // chord and R3's cursor recenter are retired, and PADFIX2's L3
    // auto-cast latch stays removed — L3 mirrors '9' like any other
    // skill button, nothing is ever held without a physical hold.)
    gDriving = moving;

    }   // Train 64: end of the Classic sticks block (buttons above are unified)

    // ── PADFIX2: divergence watchdog (left button) ──────────────────────────
    // The game can drop its held flag (MouseLButton, DAT_083a42c4) behind
    // this module's back WITHOUT a scene change — e.g. the F3/04 revival
    // handler (Net/Recv/Recv_Character.cpp) zeroes it when the hero respawns
    // in town. The owner mask only emits edges, so after such a clear a
    // still-held stick produces no fresh LeftDown and pad movement
    // is silently dead. If the mask says "held" while the game says "up"
    // for several consecutive ticks AND the cursor is over the world (over
    // a window the game legitimately consumes the flag — DAT_07d78094 is
    // MouseOnWindow), re-assert the button once so both sides agree again.
    // Rate-limited so a game path that keeps clearing cannot be spammed.
    {
        static int    s_divergeTicks = 0;
        static Uint64 s_lastHealMs = 0;
        if (gLeftOwners != 0 && DAT_083a42c4 == 0 && DAT_07d78094 == 0) {
            ++s_divergeTicks;
            if (s_divergeTicks >= 5 && now - s_lastHealMs >= 500) {
                PadLog("PAD: resync - game dropped virtual LMB while pad holds (owners=0x%X); re-asserting", gLeftOwners);
                SendMouse(Platform::InputEventType::LeftDown, gCursorX, gCursorY);
                s_lastHealMs = now;
                s_divergeTicks = 0;
            }
        } else {
            s_divergeTicks = 0;
        }
    }
}
