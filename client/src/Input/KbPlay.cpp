// KbPlay.cpp — MEAMU keyboard-only play (2026-11-13).
//
// See KbPlay.h for the map. Implementation notes:
//
// * Casting never talks to game logic directly. On a digit press the module
//   selects the quick-slot skill (same writes the bottom-bar grid and the
//   gamepad make), parks the virtual cursor on the aim target through
//   Platform::DeliverSDLInput and holds the virtual right button — the
//   stock right-click-hold pipeline (hover -> SelectedCharacter -> attack
//   stage -> skill packet) does everything else, including walking into
//   range. Release (or a broken gate) sends the matching RightUp.
// * Digit selection composes with stock: Chat_InputTick already selects the
//   Ctrl+N-assigned skill while a digit is held. This module adds the
//   gamepad's fallback on the press edge (assignment -> Nth learned skill)
//   plus the cast itself. Ctrl/Alt held = assignment/macro gestures, never
//   a cast.
// * Aim rules per skill type:
//     - Elf heal/buffs (types 26, 27, 28, 48): self-cast — cursor parked on
//       the hero, exactly like right-clicking yourself. (Stock mouse play
//       can click a party member to buff them; keyboard v1 self-buffs.)
//     - Everything else: the aim monster — Tab lock first, then the hero's
//       current/last attack target, then nearest by tile distance with a
//       facing-arc tie-break — re-acquired live as targets die. With no
//       monster available the cursor parks on the hero, so ground/AoE
//       skills cast at your feet and target skills simply have no target,
//       per stock behavior. Teleport-type skills aim at the monster's
//       ground position the same way.
// * While a cast key is held the cursor belongs to the cast: arrow-key
//   movement keeps its left button held, so the hero homes to the target
//   (click-move semantics). Release the cast key to steer freely again.
// * The gamepad keeps priority: while the pad is driving (LT cast / stick),
//   a held digit yields its right button and re-engages when the pad stops.
// * Gates (all features): in-world scene, no chat input open, no text/IME
//   mode, no error modal — the NavMove gate set plus the chat-mode flag
//   HUD_HotkeyTick uses.
// * AUTO mode (F8): a persistent cast toggle layered on the same hold-cast
//   machinery — while ON it keeps casting the currently selected skill at
//   the aim target (Tab lock -> last target -> nearest), re-acquiring as
//   targets die, with the "AUTO" HUD indicator lit. Per G's amendment
//   (2026-11-13) you CAN chat while AUTO runs: opening the chat input does
//   NOT stop it. While chat is focused every key belongs to the chat — no
//   digit casts (Gates), no arrow movement (NavMove's own InputEnable
//   gate), no Tab changes (HUD_HotkeyTick's gates), and F8 does not
//   toggle — and closing chat hands control back with AUTO untouched.
//   AUTO ends on: F8 again (chat closed), a physical mouse-button press
//   (the player taking over), an error modal, leaving the world scene,
//   or the hero's death. A hand-held digit cast and the gamepad both
//   outrank AUTO while they drive; AUTO resumes when they stop.
//
// Sandbox proof hooks: MU_KB_SELFTEST=1 injects digit keys at the title
// screen and logs the module's gate reactions (in-world proof is G's
// playtest + the rig session recorded in the build pack).

#include "stdafx.h"
#include "Input/KbPlay.h"
#include "Input/Gamepad.h"       // Gamepad_IsDriving (cast yield)
#include "Input/NavMove.h"       // NavMove_ArrowsHeld (NPC click guard)
#include "Platform/IInput.h"     // Platform::InputEvent, GetInput
#include "Core/Window.h"         // gWindow
#include "Entity/EntityTypes.h"  // CHARACTER, CHARACTER_STRIDE
#include "Entity/EntityName.h"   // EntityName::Get
#include <SDL3/SDL.h>            // SDL_GetTicks
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void DbgLogPublic(const char* msg);

namespace Platform {
void DeliverSDLInput(const InputEvent& event);   // SDLWindow.cpp
void InjectVirtualKey(int vk, bool down);        // SDLInput.cpp (rev.5 export)
}

namespace {

// ── Tuning ───────────────────────────────────────────────────────────────────
constexpr int kDigitVk[10]   = { 0x31, 0x32, 0x33, 0x34, 0x35,
                                 0x36, 0x37, 0x38, 0x39, 0x30 };  // 1..9, 0
constexpr int kDigitVal[10]  = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };
constexpr int kTabRadius     = 12;   // tiles (Chebyshev) for Tab candidates
constexpr int kNpcRadius     = 15;   // tiles for Enter (hero walks the rest)
constexpr int kAutoVk        = 0x77; // VK_F8 — AUTO toggle. Free: no
                                     // PressKey/KeyState consumer anywhere
                                     // (F9 = benchmark, Debug/MU_BENCHMARK
                                     // only; F10/F11 = camera, F12 = FPS)

// ── State ────────────────────────────────────────────────────────────────────
bool gDigitPrev[10] = {};        // raw held state last frame, per digit
int  gCastVk        = 0;         // digit vk currently cast-holding (0 = none)
bool gRightDown     = false;     // this module's virtual RMB state
bool gYielded       = false;     // cast held but pad owns the right button
int  gLockIdx       = -1;        // Tab-locked CharactersClient index
int  gAimIdx        = -1;        // current cast aim (CharactersClient index)
int  gLastIdx       = -1;        // hero's last valid attack target
bool gNpcClickUp    = false;     // Enter NPC click: LeftUp due next tick
bool gEnterPrev     = false;
float gCurX = 0.0f, gCurY = 0.0f;   // last parked cursor (window px)
bool gAutoOn        = false;     // AUTO mode (F8) master state
bool gF8Prev        = false;     // toggle key last frame
bool gMouseLPrev    = false;     // physical mouse buttons last frame
bool gMouseRPrev    = false;
bool gAutoYielded   = false;     // AUTO on, gamepad owns the right button
bool gChatPrev      = false;     // chat focus last frame (transition logs)

// ── Self-test state ──────────────────────────────────────────────────────────
bool   gSelfTest   = false;
bool   gInitTried  = false;
Uint64 gSelfT0     = 0;
int    gSelfStep   = -1;
unsigned gSelfFrame = 0;
bool   gSelfChatSim = false;     // self-test: simulated chat focus (AUTO)
bool   gSelfForced  = false;     // self-test: AUTO force-on at title

void KbLog(const char* fmt, ...)
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
    KbLog("KB: %s", buf);
}

void SendMouse(Platform::InputEventType type, float x, float y)
{
    Platform::InputEvent ev{};
    ev.type = type;
    ev.x = (int)lroundf(x);
    ev.y = (int)lroundf(y);
    Platform::DeliverSDLInput(ev);
}

void ParkCursor(float x, float y)
{
    gCurX = x; gCurY = y;
    SendMouse(Platform::InputEventType::MouseMove, x, y);
}

// Project a character's world position to window pixels (the projection
// returns 640x480 UI space; DeliverSDLInput takes real window pixels).
void ParkOnCharacter(CHARACTER* c)
{
    int sx = 0, sy = 0;
    Camera_ProjectWorldToScreen((float*)c->Object.Position, &sx, &sy);
    const float winW = (float)gWindow.GetRealWidth();
    const float winH = (float)gWindow.GetRealHeight();
    ParkCursor(sx * winW / 640.0f, sy * winH / 480.0f);
}

void SetRightButton(bool down)
{
    if (down == gRightDown) return;
    gRightDown = down;
    SendMouse(down ? Platform::InputEventType::RightDown
                   : Platform::InputEventType::RightUp, gCurX, gCurY);
}

bool KeyHeld(int vk)
{
    return ((unsigned short)Platform::GetInput().KeyState(vk) & 0x8000) != 0;
}

// The gate set every keyboard-play feature shares.
bool Gates()
{
    return SceneFlag == 5 && Hero != 0 && CharactersClient != 0 &&
           InputEnable == 0 && GuildInputEnable == 0 && DAT_07e11d71 == 0 &&
           DAT_083a7c24 == 0;
}

CHARACTER* HeroC() { return (CHARACTER*)(uintptr_t)Hero; }

CHARACTER* EntryAt(int idx)
{
    return (CHARACTER*)((char*)(uintptr_t)CharactersClient + idx * CHARACTER_STRIDE);
}

int Cheb(CHARACTER* a, CHARACTER* b)
{
    const int dx = std::abs(a->PositionX - b->PositionX);
    const int dy = std::abs(a->PositionY - b->PositionY);
    return dx > dy ? dx : dy;
}

// An attackable monster in the client table: live monster-kind entry, not
// dead, not the hero. (Kind 0x22 is the hover system's monster class;
// MonsterIndex 0xFF marks players.) The server still enforces the rest —
// own summons, safe zones — exactly as it does for mouse attacks.
bool ValidMonster(int idx)
{
    if (idx < 0 || idx >= 400 || !Hero) return false;
    CHARACTER* c = EntryAt(idx);
    if ((char*)c == (char*)(uintptr_t)Hero) return false;
    if (c->Object.Live == 0) return false;
    if (c->Object.Kind != 0x22) return false;
    if (c->Dead != 0) return false;
    if (c->MonsterIndex == 0xFF) return false;
    return true;
}

// Acquire the aim monster for a cast: Tab lock, then last target, then
// nearest (tile distance, facing-arc tie-break) within acquisition range.
int AcquireTarget(int skillDist)
{
    CHARACTER* hero = HeroC();
    int acq = skillDist + 8;
    if (acq < 8) acq = 8;
    if (acq > 14) acq = 14;

    if (ValidMonster(gLockIdx) && Cheb(hero, EntryAt(gLockIdx)) <= acq + 2)
        return gLockIdx;
    if (ValidMonster(gLastIdx) && Cheb(hero, EntryAt(gLastIdx)) <= acq)
        return gLastIdx;

    int   best = -1, bestDist = 0;
    float bestArc = 0.0f;
    for (int i = 0; i < 400; ++i) {
        if (!ValidMonster(i)) continue;
        CHARACTER* c = EntryAt(i);
        const int d = Cheb(hero, c);
        if (d > acq) continue;
        typedef float (__cdecl *CreateAngleFn)(float, float, float, float);
        const float ang = ((CreateAngleFn)&CreateAngle)(
            hero->Object.Position[0], hero->Object.Position[1],
            c->Object.Position[0],    c->Object.Position[1]);
        const float arc = fabsf((float)FarAngle(hero->Object.Angle[2], ang, '\x01'));
        if (best < 0 || d < bestDist || (d == bestDist && arc < bestArc)) {
            best = i; bestDist = d; bestArc = arc;
        }
    }
    return best;
}

// ── Skill selection (mirrors the gamepad/bottom-bar writes) ──────────────────
void SelectSkillSlot(int slot, const char* why)
{
    if (!Hero || !CharacterAttribute || slot < 0 || slot >= 60) return;
    HeroC()->CurrentSkill = (BYTE)slot;
    DAT_0055a3e4 = (DWORD)slot;                 // bottom-bar highlight mirror
    PlayBuffer(0x19, nullptr, 0);               // the grid's selection click
    KbLog("KB: skill select slot %d (type %d) - %s", slot,
          (unsigned char)((char*)CharacterAttribute)[slot + 87], why);
}

// Gamepad QuickSlot semantics with the stock digit numbering: the Ctrl+N
// assignment value is the digit itself (1..9, and 0 for the '0' key);
// fallback = the Nth learned skill ('0' counts as the 10th).
void QuickSlotDigit(int digitVal)
{
    if (!Hero || !CharacterAttribute) return;
    char* CA = (char*)CharacterAttribute;
    const int row = (int)SelectedHero;
    for (int i = 0; i < 60; ++i)
        if (CA[i + 87] != 0 && (unsigned char)CA[(row << 6) + i + 215] == (unsigned char)digitVal) {
            SelectSkillSlot(i, "digit (hotkey assignment)");
            return;
        }
    const int nth = (digitVal == 0) ? 10 : digitVal;
    int seen = 0;
    for (int i = 0; i < 60; ++i)
        if (CA[i + 87] != 0 && ++seen == nth) {
            SelectSkillSlot(i, "digit (nth learned fallback)");
            return;
        }
    KbLog("KB: digit %d - no skill assigned or learned at that slot", digitVal);
}

bool IsSelfSkill(int skillType)
{
    // Elf heal/buff line: Heal, Greater Defense, Greater Damage, Greater Life.
    return skillType == 26 || skillType == 27 || skillType == 28 || skillType == 48;
}

int SelectedSkillType()
{
    if (!Hero || !CharacterAttribute) return 0;
    const int slot = HeroC()->CurrentSkill;
    if (slot < 0 || slot >= 60) return 0;
    return (unsigned char)((char*)CharacterAttribute)[slot + 87];
}

int SelectedSkillDist()
{
    int dist = 0;
    GetSkillInformation(SelectedSkillType(), 1, nullptr, nullptr, &dist, nullptr);
    return dist;
}

// ── Cast hold ────────────────────────────────────────────────────────────────
void StopCast(const char* why)
{
    if (!gCastVk) return;
    KbLog("KB: cast stop (%s)", why);
    gCastVk = 0;
    gAimIdx = -1;
    gYielded = false;
    SetRightButton(false);
}

void AimAndPark()
{
    CHARACTER* hero = HeroC();
    if (IsSelfSkill(SelectedSkillType())) {
        gAimIdx = -1;
        ParkOnCharacter(hero);
        return;
    }
    int t = gAimIdx;
    if (!ValidMonster(t) || Cheb(hero, EntryAt(t)) > SelectedSkillDist() + 16)
        t = AcquireTarget(SelectedSkillDist());
    gAimIdx = t;
    ParkOnCharacter(t >= 0 ? EntryAt(t) : hero);
}

void OnDigitPress(int vk, int digitVal)
{
    QuickSlotDigit(digitVal);
    if (SelectedSkillType() == 0) return;   // empty slot: select behavior only
    gCastVk = vk;
    gYielded = false;
    AimAndPark();
    if (!Gamepad_IsDriving()) SetRightButton(true);
    else gYielded = true;
    KbLog("KB: cast hold start - digit %d, skill type %d, aim %d",
          digitVal, SelectedSkillType(), gAimIdx);
}

// ── Enter: talk to the nearest NPC ───────────────────────────────────────────
void OnEnterPress()
{
    if (NavMove_ArrowsHeld() || Gamepad_IsDriving()) return;
    CHARACTER* hero = HeroC();
    int best = -1, bestDist = 0;
    for (int i = 0; i < 400; ++i) {
        CHARACTER* c = EntryAt(i);
        if ((char*)c == (char*)hero) continue;
        if (c->Object.Live == 0 || c->Object.Kind != 4 || c->Dead != 0) continue;
        const int d = Cheb(hero, c);
        if (d <= kNpcRadius && (best < 0 || d < bestDist)) { best = i; bestDist = d; }
    }
    if (best < 0) { KbLog("KB: Enter - no NPC in range"); return; }
    ParkOnCharacter(EntryAt(best));
    SendMouse(Platform::InputEventType::LeftDown, gCurX, gCurY);
    gNpcClickUp = true;   // LeftUp next tick, like a real click
    Say("Talk: %s", EntityName::Get((char*)EntryAt(best)));
}

// ── AUTO mode (F8) helpers ───────────────────────────────────────────────────
// Chat focus = the chat input line is open (InputEnable, DAT_00559c84 — the
// flag Input_WndProc's Enter handler toggles, covering login + chat). The
// self-test can also simulate it at the title screen.
bool ChatFocused()
{
    // During the title-screen exercise the simulation is authoritative
    // (the login screen's own text state would only add noise).
    if (gSelfTest && gSelfForced) return gSelfChatSim;
    return InputEnable != 0 || (gSelfTest && gSelfChatSim);
}

// The gates for AUTO's own drive — deliberately NOT the keyboard Gates():
// chat focus must not stop AUTO (G's amendment), and neither must the
// guild/IME text flags. Scene, hero presence and the error modal bound it.
bool AutoGates()
{
    return SceneFlag == 5 && Hero != 0 && CharactersClient != 0 &&
           DAT_083a7c24 == 0;
}

bool HeroDead()
{
    return Hero != 0 && HeroC()->Dead != 0;
}

void AutoSet(bool on, const char* why, bool announce)
{
    if (on == gAutoOn) return;
    gAutoOn = on;
    gAutoYielded = false;
    // A hand-held digit cast owns the shared right button otherwise; its
    // maintenance block keeps it held without help from here.
    if (!on && !gCastVk) SetRightButton(false);
    if (announce && SceneFlag == 5) Say("Auto-attack %s", on ? "ON" : "OFF");
    KbLog("KB-AUTO: %s (%s)", on ? "ON" : "OFF", why);
}

// ── Self-test (title screen; proves dispatch + gates without a server) ───────
void SelfTestDrive()
{
    if (SceneFlag < 2) return;
    if (!gSelfT0) {
        gSelfT0 = SDL_GetTicks();
        KbLog("KB-SELFTEST: title reached - timeline starts");
        return;
    }
    // Frame-counted steps, not wall-clock: at sandbox frame rates (~1-2 fps
    // under load) a seconds-based timeline compresses and skips steps.
    ++gSelfFrame;
    const int step = (int)(gSelfFrame / 45);
    if (step == gSelfStep) return;
    gSelfStep = step;
    switch (step) {
    case 1: KbLog("KB-SELFTEST: injecting '1' down"); Platform::InjectVirtualKey(0x31, true);  break;
    case 2:
        KbLog("KB-SELFTEST: key table readback '1' held=%d (injection visible to KeyHeld)",
              KeyHeld(0x31) ? 1 : 0);
        KbLog("KB-SELFTEST: injecting '1' up");
        Platform::InjectVirtualKey(0x31, false);
        break;
    case 3: KbLog("KB-SELFTEST: injecting Enter down"); Platform::InjectVirtualKey(13, true);  break;
    case 4: KbLog("KB-SELFTEST: injecting Enter up");   Platform::InjectVirtualKey(13, false); break;
    case 5: KbLog("KB-SELFTEST: digit/enter segment done - AUTO segment follows"); break;
    case 6: KbLog("KB-AUTO-SELFTEST: injecting F8 down (title - toggle must be ignored)");
        Platform::InjectVirtualKey(kAutoVk, true);  break;
    case 7: KbLog("KB-AUTO-SELFTEST: F8 up; forcing AUTO on for the state-machine exercise");
        Platform::InjectVirtualKey(kAutoVk, false);
        gSelfForced = true;
        AutoSet(true, "self-test force-on", false);
        break;
    case 8: KbLog("KB-AUTO-SELFTEST: simulated chat opens - AUTO must stay on");
        gSelfChatSim = true; break;
    case 9: KbLog("KB-AUTO-SELFTEST: typing '1' + F8 with chat focused - both belong to chat");
        Platform::InjectVirtualKey(0x31, true);
        Platform::InjectVirtualKey(kAutoVk, true); break;
    case 10: KbLog("KB-AUTO-SELFTEST: releasing typed keys");
        Platform::InjectVirtualKey(0x31, false);
        Platform::InjectVirtualKey(kAutoVk, false); break;
    case 11: KbLog("KB-AUTO-SELFTEST: simulated chat closes - control returns, AUTO still on=%d",
               gAutoOn ? 1 : 0);
        gSelfChatSim = false; break;
    case 12: KbLog("KB-AUTO-SELFTEST: injecting F8 down - AUTO must toggle off");
        Platform::InjectVirtualKey(kAutoVk, true); break;
    case 13: Platform::InjectVirtualKey(kAutoVk, false);
        gSelfForced = false;
        KbLog("KB-AUTO-SELFTEST: timeline complete (AUTO on=%d, expect 0)", gAutoOn ? 1 : 0);
        break;
    default: break;
    }
}

} // namespace

// Tab hook for HUD_HotkeyTick: cycle the combat lock among attackable
// monsters in range (nearest first, wrapping). True = Tab consumed.
bool KbPlay_TabCycle()
{
    if (!Gates()) return false;
    CHARACTER* hero = HeroC();

    int idxs[400];
    int dists[400];
    int n = 0;
    for (int i = 0; i < 400; ++i) {
        if (!ValidMonster(i)) continue;
        const int d = Cheb(hero, EntryAt(i));
        if (d <= kTabRadius) { idxs[n] = i; dists[n] = d; ++n; }
    }
    if (n == 0) return false;

    // Insertion sort by (distance, index) — n is small in practice.
    for (int a = 1; a < n; ++a) {
        const int iv = idxs[a], dv = dists[a];
        int b = a - 1;
        while (b >= 0 && (dists[b] > dv || (dists[b] == dv && idxs[b] > iv))) {
            idxs[b + 1] = idxs[b]; dists[b + 1] = dists[b]; --b;
        }
        idxs[b + 1] = iv; dists[b + 1] = dv;
    }

    int next = idxs[0];
    for (int a = 0; a < n; ++a)
        if (idxs[a] == gLockIdx) { next = idxs[(a + 1) % n]; break; }

    gLockIdx = next;
    ParkOnCharacter(EntryAt(next));   // hover shows the stock target display
    Say("Target: %s", EntityName::Get((char*)EntryAt(next)));
    return true;
}

void KbPlay_Tick()
{
    if (!gInitTried) {
        gInitTried = true;
        gSelfTest = (getenv("MU_KB_SELFTEST") != nullptr);
        KbLog("KB: module init%s", gSelfTest ? " (self-test)" : "");
    }
    if (gSelfTest) SelfTestDrive();

    // Finish the Enter NPC click started last tick.
    if (gNpcClickUp) {
        gNpcClickUp = false;
        SendMouse(Platform::InputEventType::LeftUp, gCurX, gCurY);
    }

    const bool gates = Gates();

    // Chat focus transitions. Chat is tracked apart from Gates(): AUTO
    // keeps running while the chat input is open (G's amendment) — only
    // the keyboard changes hands.
    const bool chat = ChatFocused();
    if (chat != gChatPrev) {
        if (gAutoOn)
            KbLog("KB-AUTO: chat %s - %s", chat ? "opened" : "closed",
                  chat ? "AUTO keeps running; keyboard belongs to chat"
                       : "AUTO still on; keyboard control restored");
        gChatPrev = chat;
    }

    // Track the hero's live attack target (the "last target" preference).
    // Target tracking is not keyboard input, so it follows AutoGates():
    // AUTO's own casts keep feeding it while the player chats, and a chat
    // session no longer clears the Tab lock AUTO is aiming with.
    if (AutoGates() && Attacking >= 0 && ValidMonster(Attacking))
        gLastIdx = Attacking;
    // A dead/gone lock clears silently.
    if (gLockIdx >= 0 && (!AutoGates() || !ValidMonster(gLockIdx)))
        gLockIdx = -1;

    // ── Digits: press = select + cast once, hold = repeat-cast ───────────────
    const bool modifiers = KeyHeld(0x11) || KeyHeld(0x12);   // Ctrl / Alt
    for (int i = 0; i < 10; ++i) {
        const int  vk   = kDigitVk[i];
        const bool held = KeyHeld(vk);
        if (held && !gDigitPrev[i]) {
            if (gSelfTest && gSelfChatSim)
                KbLog("KB-AUTO-SELFTEST: digit %d press yielded to chat (no cast, no select)", kDigitVal[i]);
            else if (gates && !modifiers) OnDigitPress(vk, kDigitVal[i]);
            else if (gSelfTest)
                KbLog("KB-SELFTEST: digit %d press ignored (gates=%d modifiers=%d scene=%d)",
                      kDigitVal[i], gates ? 1 : 0, modifiers ? 1 : 0, SceneFlag);
        }
        if (!held && gDigitPrev[i] && gCastVk == vk)
            StopCast("key released");
        gDigitPrev[i] = held;
    }

    // ── Held cast: maintain aim + button, honor gates and pad priority ───────
    if (gCastVk) {
        if (!gates) {
            StopCast("gate closed");
        } else if (Gamepad_IsDriving()) {
            if (!gYielded) { gYielded = true; SetRightButton(false); KbLog("KB: cast yields to gamepad"); }
        } else {
            if (gYielded) { gYielded = false; KbLog("KB: cast re-engages"); }
            AimAndPark();
            SetRightButton(true);
        }
    }

    // ── AUTO mode: toggle, manual override, drive ────────────────────────────
    const bool f8 = KeyHeld(kAutoVk);
    if (f8 && !gF8Prev) {
        if (chat) {
            if (gSelfTest)
                KbLog("KB-AUTO-SELFTEST: F8 pressed while chat focused - no toggle (key belongs to chat)");
        } else if (gAutoOn) {
            AutoSet(false, "toggle key", true);
        } else if (AutoGates() && !HeroDead()) {
            AutoSet(true, "toggle key", true);
        } else if (gSelfTest) {
            KbLog("KB-AUTO-SELFTEST: F8 toggle ignored (autogates=%d dead=%d scene=%d)",
                  AutoGates() ? 1 : 0, HeroDead() ? 1 : 0, SceneFlag);
        }
    }
    gF8Prev = f8;

    // A physical mouse press is the player taking control back. The key
    // table's button bits are written only by real SDL device events (pad
    // key pulses never use buttons 1/2); virtual buttons from this module,
    // NavMove and the gamepad travel via DeliverSDLInput and never appear
    // here, so this cannot misfire on synthesized input.
    const bool mL = KeyHeld(1), mR = KeyHeld(2);
    if (gAutoOn && ((mL && !gMouseLPrev) || (mR && !gMouseRPrev)))
        AutoSet(false, "manual mouse input", true);
    gMouseLPrev = mL; gMouseRPrev = mR;

    if (gAutoOn) {
        if (gSelfTest && gSelfForced) {
            // Title-screen exercise: the drive/off checks below need a
            // world; they are covered by the playtest, not here.
        } else if (!AutoGates()) {
            AutoSet(false, SceneFlag != 5 ? "scene change" : "gate closed (modal)", false);
        } else if (HeroDead()) {
            AutoSet(false, "character death", true);
        } else if (Gamepad_IsDriving()) {
            if (!gAutoYielded) {
                gAutoYielded = true;
                if (!gCastVk) SetRightButton(false);
                KbLog("KB-AUTO: yields to gamepad");
            }
        } else if (!gCastVk) {   // a hand-held digit cast outranks AUTO
            if (gAutoYielded) { gAutoYielded = false; KbLog("KB-AUTO: re-engages"); }
            if (SelectedSkillType() != 0) { AimAndPark(); SetRightButton(true); }
            else SetRightButton(false);   // empty slot selected: stand + scan
        }
    }

    // ── Enter: NPC talk ──────────────────────────────────────────────────────
    const bool enter = KeyHeld(13);
    if (enter && !gEnterPrev) {
        if (gates) OnEnterPress();
        else if (gSelfTest)
            KbLog("KB-SELFTEST: Enter press ignored (gates closed, scene=%d)", SceneFlag);
    }
    gEnterPrev = enter;
}

// HUD indicator (RenderInformation, next to the gamepad's "PAD"): lit for
// the whole AUTO run, chat sessions included — it reads only the state.
void KbPlay_RenderAutoIndicator()
{
    if (!gAutoOn) return;
    UI_RenderText(532, 6, (char*)"AUTO", (LPSIZE)0x0, '\0', 0);
}
