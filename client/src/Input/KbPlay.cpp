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
// * The gamepad keeps priority: while the pad is driving (stick move),
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
//   AUTO ends on: F8 again (chat closed), a NEW physical mouse-button
//   press (the player taking over; releasing the hold that armed AUTO is
//   not one — its RightUp clears the shared hold flag the Attack gate
//   reads, so the drive re-asserts the hold, see AutoDriveButton), an
//   error modal, leaving the world scene,
//   or the hero's death. A hand-held digit cast and the gamepad both
//   outrank AUTO while they drive; AUTO resumes when they stop.
//
// * Cast-streak AUTO latch (MEAMU, G's ask 2026-11-13, design revised
//   same night): casting the selected skill continuously for 2.0 s --
//   casts never more than ~0.6 s apart -- latches AUTO on through the
//   same AutoSet path as F8 (same banner + indicator). The trigger is
//   the cast itself, not any one input: held digit keys and repeated
//   clicks count, because every
//   source funnels into Combat_DispatchHeroSkillAttack, whose
//   branches stamp SkillStage = 1 at the cast-commit point; the
//   tick polls that stamp for rising edges. TWO sources are excluded
//   since 2026-11-16 (RMB law + pad law, below): casts made while the
//   physical right mouse button is held, and casts driven by a held
//   gamepad skill button. The streak builds only
//   while AUTO is off, chat is closed and the world gates pass — the
//   keyboard master switch does NOT gate it (decouple, 2026-11-15:
//   a gamepad/click streak is not keyboard input); chat, a scene
//   change, an error modal or the hero's death resets it.
//   Anti-re-latch: when AUTO turns off (say a mouse press stops it
//   mid-hold), no new streak can arm until casting has fully stopped
//   for ~0.8 s, so the same hold that was interrupted can never
//   instantly re-latch.
//   RMB law (MEAMU 2026-11-16, G's order): the mouse is OUT of the
//   streak — a cast observed while the physical right button is held
//   stamps the beat clock but kills the span, so holding RMB can
//   never latch AUTO. Stock hold-to-attack (the Combat gate on the
//   shared MouseRButton flag) is a different path and is untouched;
//   digit holds and clicks streak exactly as before.
//   Pad law (MEAMU 2026-11-16, G's order): gamepad skill-button casts
//   are OUT of the streak too — a cast observed while any pad skill
//   button is held (Gamepad_PadCastHeld: A/X/Y/B/RB/LB/RT/LT/L3/R3,
//   all digit mirrors since PADCAST) stamps the beat clock but kills
//   the span, so holding a pad button to repeat-cast can never latch
//   AUTO, and casting still stops the moment the button is released.
//   Real-keyboard digit holds are untouched and latch as before.
//
// * Master toggle (MEAMU kb-toggle, 2026-11-13): Config.ini [Keyboard]
//   Enable (default 1 when missing) decides the boot state; F9 flips it
//   in game under the AUTO toggle's gate discipline and persists the new
//   value. OFF folds into the shared Gates() so digits/Tab/Enter/Space
//   all revert to stock behavior at once. AUTO is NOT part of the
//   switch (MEAMU decouple, 2026-11-15, G's order): F8 toggles AUTO
//   with the switch on or off, a flip mid-AUTO leaves AUTO running,
//   and the cast-streak latch arms either way — AUTO's drive runs on
//   AutoGates(), never the keyboard gates. Gamepad input has no
//   connection to the switch either.
//
// Sandbox proof hooks: MU_KB_SELFTEST=1 injects digit keys at the title
// screen and logs the module's gate reactions (in-world proof is G's
// playtest + the rig session recorded in the build pack).

#include "stdafx.h"
#include "Input/KbPlay.h"
#include "Config/UserSettings.h" // gUserSettings ([Keyboard] Enable)
#include "Input/Gamepad.h"       // Gamepad_IsDriving (cast yield), Gamepad_PadCastHeld (pad law)
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
                                     // PressKey/KeyState consumer anywhere.
constexpr int kToggleVk      = 0x78; // VK_F9 — keyboard controls ON/OFF
                                     // (master toggle). Free in every shipped
                                     // build: the only VK_F9 consumer in the
                                     // tree is the MU_BENCHMARK test rig
                                     // (Debug/Benchmark.cpp), compiled only
                                     // for Debug configs or with
                                     // -DMU_BENCHMARK=ON (default OFF) —
                                     // published releases never define it.
                                     // (F10/F11 = camera, F12 = FPS counter,
                                     // so the F-key fallbacks are all taken.)
constexpr Uint64 kLatchSpanMs  = 2000; // continuous casting that latches AUTO
constexpr Uint64 kLatchGapMs   = 600;  // max silence between casts in a streak
constexpr Uint64 kLatchRearmMs = 800;  // cast silence required after AUTO off

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
bool gKbEnabled     = true;      // keyboard controls master switch
                                 // ([Keyboard] Enable at boot, F9 in game)
bool gF9Prev        = false;     // master toggle key last frame
bool gMouseLPrev    = false;     // physical mouse buttons last frame
bool gMouseRPrev    = false;
bool gAutoYielded   = false;     // AUTO on, gamepad owns the right button
Uint64 gStreakStart = 0;         // cast-streak latch: first cast of the span
Uint64 gLastCastT   = 0;         // cast-streak latch: most recent cast stamp
BYTE gSkillStagePrev = 0;        // hero SkillStage last tick (edge detect)
bool gLatchSuppress = false;     // anti-re-latch: waiting out the silence beat
Uint64 gSuppressT   = 0;         // when the current suppression engaged
bool gAutoPrevLatch = false;     // AUTO state last tick (off-transition watch)
Uint64 gLatchSimT   = 0;         // self-test: simulated latch clock (0 = real)
bool gChatPrev      = false;     // chat focus last frame (transition logs)

// ── Self-test state ──────────────────────────────────────────────────────────
bool   gSelfTest   = false;
bool   gInitTried  = false;
Uint64 gSelfT0     = 0;
int    gSelfStep   = -1;
unsigned gSelfFrame = 0;
bool   gSelfChatSim = false;     // self-test: simulated chat focus (AUTO)
bool   gSelfForced  = false;     // self-test: AUTO force-on at title
bool   gSelfWorldSim = false;    // self-test: F9 world gates simulated

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

// The keyboard-controls master switch (Config.ini [Keyboard] Enable at
// boot, F9 in game). Every KbPlay input path consults it; the gamepad
// never does.
bool KbEnabled() { return gKbEnabled; }

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
    return skillType == 26 || skillType == 27 || skillType == 28 || skillType == 48 || skillType == 57 || skillType == 58 || skillType == 59 || skillType == 60 || skillType == 61;  // 57 = Omni Buff (MEAMU 2026-11-14), 58 = Mangekyou Sharingan (MEAMU 2026-11-15), 59 = Sage Mode (MEAMU 2026-11-17), 60 = Jinchuriki Mode + 61 = Biju Mode (MEAMU 2026-11-17, Train 4)
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

// AUTO's button step — keep the synthetic hold alive in the game's
// shared MouseRButton flag (DAT_083a42ac). That flag is the hold the
// hero Attack gate reads (Combat.cpp: MouseRButtonPush || MouseRButton),
// and Input_WndProc sets/clears it on RightDown/RightUp events from ANY
// source: real device events and this module's synthetic ones share the
// one flag. So when AUTO latches out of a physical mouse hold and the
// player then lets go of the button — the natural "AUTO is on, hands
// off" move — the real RightUp clears the flag under us. gRightDown
// cannot see that clobber: it tracks only our own sends, and the
// KeyHeld key table is written by real SDL device events only, so the
// drive's SetRightButton(true) would no-op forever after the release:
// AUTO lit, hero idle (G's report, 2026-11-14). The digit path never
// hits this because no physical button is involved there. Repair:
// whenever the shared flag is down, forget our press and send a fresh
// RightDown to re-assert the hold. A NEW physical press is unaffected —
// the press-edge cancel in the tick samples it before the drive runs
// and takes AUTO down first, exactly as on the F8 path.
void AutoDriveButton()
{
    if (DAT_083a42ac == 0) gRightDown = false;
    SetRightButton(true);
}

// ── Cast-streak AUTO latch ──────────────────────────────────────────────────
// LatchNow is the streak clock; the self-test substitutes a simulated
// time so the state machine can be exercised deterministically at the
// title screen (no world, no real casts there).
Uint64 LatchNow()
{
    return (gSelfTest && gLatchSimT != 0) ? gLatchSimT : SDL_GetTicks();
}

// One observed cast of the player's selected skill. In the real path
// this is fed by the SkillStage rising edge in LatchFrame; the
// self-test feeds synthetic stamps through the same entry. mouseHeld
// marks a cast excluded from the streak: made while the physical
// right mouse button is held (RMB law), or while a gamepad skill
// button drives the cast (pad law — the caller ORs Gamepad_PadCastHeld()
// in; the parameter keeps its original name for the self-test's sake).
void LatchNoteCast(Uint64 now, bool mouseHeld = false)
{
    const Uint64 prev = gLastCastT;
    gLastCastT = now;
    // Decoupled from the keyboard master switch (MEAMU 2026-11-15):
    // the streak is fed by digit holds, gamepad casts and clicks, so
    // the F9 switch never gated it legitimately.
    if (gAutoOn || gLatchSuppress) { gStreakStart = 0; return; }
    // RMB law (MEAMU 2026-11-16, G's order): a mouse-held cast never
    // builds the streak — it kills the span outright, so no mixture
    // of mouse casting can ever latch AUTO. The gLastCastT stamp
    // above still lands, so the anti-re-latch beat keeps measuring
    // from the player's last cast of ANY source, exactly as before.
    if (mouseHeld) { gStreakStart = 0; return; }
    if (gStreakStart == 0 || prev == 0 || now - prev > kLatchGapMs) {
        gStreakStart = now;   // first cast, or the gap broke the old span
        return;
    }
    if (now - gStreakStart >= kLatchSpanMs) {
        gStreakStart = 0;
        AutoSet(true, "cast-streak latch", true);
    }
}

// Per-tick latch bookkeeping: poll the hero's SkillStage for cast
// edges, reset the streak when the world moves on, and run the
// anti-re-latch beat off AUTO's on->off transitions.
void LatchFrame(bool chat)
{
    const Uint64 now = LatchNow();
    const BYTE stage = Hero ? HeroC()->SkillStage : 0;
    // KeyHeld(2) reads the key table's button bit, which only real
    // SDL device events write (synthetic buttons from this module,
    // NavMove and the gamepad travel via DeliverSDLInput and never
    // appear there) — it is the physical right mouse button, exactly
    // as the press-edge cancel in the tick samples it. Casts observed
    // while it is held are mouse casts: the RMB law excludes them
    // from the streak (see LatchNoteCast).
    // Gamepad_PadCastHeld() is the pad twin of that sample: it reports
    // the gamepad module's own mirror state (any of the ten skill
    // buttons held), which the key table cannot distinguish from a
    // physical digit hold. Casts observed while it is set are pad
    // casts: the pad law excludes them the same way. A real-keyboard
    // digit hold sets neither flag and streaks exactly as before.
    if (stage == 1 && gSkillStagePrev != 1)
        LatchNoteCast(now, KeyHeld(2) || Gamepad_PadCastHeld());
    gSkillStagePrev = stage;

    const bool world = AutoGates() || (gSelfTest && gSelfWorldSim);
    if (chat || !world || HeroDead()) gStreakStart = 0;

    if (gAutoPrevLatch && !gAutoOn) {
        gLatchSuppress = true;   // AUTO just turned off: arm the beat
        gSuppressT = now;
        gStreakStart = 0;
        KbLog("KB-AUTO: cast-streak latch suppressed until casting stops for %d ms",
              (int)kLatchRearmMs);
    }
    gAutoPrevLatch = gAutoOn;
    if (gLatchSuppress) {
        // The beat runs from the later of the suppression start and
        // the last cast: a quiet AUTO off clears on its own, while a
        // hold that keeps casting keeps the beat alive.
        const Uint64 beatFrom = gLastCastT > gSuppressT ? gLastCastT : gSuppressT;
        if (now - beatFrom >= kLatchRearmMs) {
            gLatchSuppress = false;
            KbLog("KB-AUTO: cast-streak latch re-armed (silence beat served)");
        }
    }
}

// ── Keyboard controls master switch (F9 / boot config) ──────────────────────
// OFF silences every KbPlay KEYBOARD input path at once (digit casts,
// Tab cycle, Enter talk, the Space nearest-pick). AUTO is decoupled
// from the switch (MEAMU 2026-11-15, G's order): a flip in either
// direction leaves a running AUTO untouched — AUTO's drive runs on
// AutoGates(), not the keyboard gates. A hand-held digit cast still
// stops (it IS keyboard input); AUTO's drive re-asserts the shared
// button on its next tick. ON restores the shipped behavior exactly.
// The flip applies immediately and persists to Config.ini [Keyboard]
// Enable through the settings writer (targeted write of that one
// value into the same file the client read at boot).
void KbSet(bool on, const char* why)
{
    if (on == gKbEnabled) return;
    gKbEnabled = on;
    if (!on) {
        StopCast("keyboard controls off");
        gLockIdx = -1;   // the Tab lock belongs to the feature
    }
    gUserSettings.SetKeyboardEnable(on);
    if (SceneFlag == 5) Say("KEYBOARD CONTROLS: %s", on ? "ON" : "OFF");
    KbLog("KB: keyboard controls %s (%s)", on ? "ON" : "OFF", why);
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
    case 14: KbLog("KB-TOGGLE-SELFTEST: exercise starts - world gates simulated, AUTO forced on (boot KB on=%d)",
               gKbEnabled ? 1 : 0);
        if (!gKbEnabled) { KbSet(true, "self-test normalize"); KbLog("KB-TOGGLE-SELFTEST: KB normalized ON for the toggle exercise"); }
        gSelfForced = true;
        gSelfWorldSim = true;
        AutoSet(true, "self-test toggle exercise", false);
        break;
    case 15: KbLog("KB-TOGGLE-SELFTEST: injecting F9 down - KB must flip OFF and AUTO must STAY on (decoupled)");
        Platform::InjectVirtualKey(kToggleVk, true);  break;
    case 16: Platform::InjectVirtualKey(kToggleVk, false);
        KbLog("KB-TOGGLE-SELFTEST: after F9: KB on=%d (expect 0), AUTO on=%d (expect 1 - flip leaves AUTO running)",
              gKbEnabled ? 1 : 0, gAutoOn ? 1 : 0);
        KbLog("KB-TOGGLE-SELFTEST: injecting '1' down - digit must be ignored (KB off)");
        Platform::InjectVirtualKey(0x31, true);
        break;
    case 17: Platform::InjectVirtualKey(0x31, false);
        KbLog("KB-TOGGLE-SELFTEST: injecting F8 down - AUTO must toggle OFF even with KB off (decoupled)");
        Platform::InjectVirtualKey(kAutoVk, true);  break;
    case 18: Platform::InjectVirtualKey(kAutoVk, false);
        KbLog("KB-TOGGLE-SELFTEST: after F8: AUTO on=%d (expect 0). Injecting F8 down next step - AUTO must toggle ON with KB off",
              gAutoOn ? 1 : 0);
        break;
    case 19: KbLog("KB-TOGGLE-SELFTEST: injecting F8 down - AUTO must engage with KB off (decoupled)");
        Platform::InjectVirtualKey(kAutoVk, true);  break;
    case 20: Platform::InjectVirtualKey(kAutoVk, false);
        KbLog("KB-TOGGLE-SELFTEST: after F8: AUTO on=%d (expect 1 - engaged with KB off). Injecting F9 down - KB back ON, AUTO must stay on",
              gAutoOn ? 1 : 0);
        Platform::InjectVirtualKey(kToggleVk, true);  break;
    case 21: Platform::InjectVirtualKey(kToggleVk, false);
        KbLog("KB-TOGGLE-SELFTEST: after F9: KB on=%d (expect 1), AUTO on=%d (expect 1 - flip back leaves AUTO running)",
              gKbEnabled ? 1 : 0, gAutoOn ? 1 : 0);
        AutoSet(false, "self-test toggle exercise", false);
        KbLog("KB-TOGGLE-SELFTEST: segment complete (KB on=%d expect 1, AUTO on=%d expect 0 - AUTO taken down by the test, not the switch)",
              gKbEnabled ? 1 : 0, gAutoOn ? 1 : 0);
        KbLog("KB-TOGGLE-SELFTEST: injecting '1' down - normal digit routing resumes (KB on; released next step)");
        Platform::InjectVirtualKey(0x31, true);
        break;
    case 22: Platform::InjectVirtualKey(0x31, false);   // release the toggle segment's digit probe
        KbLog("KB-LATCH-SELFTEST: streak segment starts (simulated clock; KB on=%d, AUTO on=%d, expect 1/0)",
               gKbEnabled ? 1 : 0, gAutoOn ? 1 : 0);
        gSelfForced = true;
        gSelfWorldSim = true;
        gLatchSimT = 500000;
        LatchNoteCast(gLatchSimT);            // streak opens
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        KbLog("KB-LATCH-SELFTEST: 3 casts over 800 ms: streaking=%d AUTO=%d (expect 1/0)",
              gStreakStart != 0 ? 1 : 0, gAutoOn ? 1 : 0);
        break;
    case 23: gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);   // span reaches 2000 ms
        KbLog("KB-LATCH-SELFTEST: span 2000 ms reached: AUTO=%d (expect 1 - latched)",
              gAutoOn ? 1 : 0);
        break;
    case 24: AutoSet(false, "self-test streak exercise", false);
        LatchFrame(false);   // the off transition must engage suppression
        gLatchSimT += 300; LatchNoteCast(gLatchSimT);   // player keeps casting
        gLatchSimT += 300; LatchNoteCast(gLatchSimT);
        KbLog("KB-LATCH-SELFTEST: AUTO killed mid-hold: suppress=%d streaking=%d AUTO=%d (expect 1/0/0)",
              gLatchSuppress ? 1 : 0, gStreakStart != 0 ? 1 : 0, gAutoOn ? 1 : 0);
        break;
    case 25: gLatchSimT += 900;               // full cast silence: beat served
        LatchFrame(false);
        KbLog("KB-LATCH-SELFTEST: after 900 ms silence: suppress=%d (expect 0 - re-armed)",
              gLatchSuppress ? 1 : 0);
        break;
    case 26: for (int k = 0; k < 6; ++k) { gLatchSimT += 400; LatchNoteCast(gLatchSimT); }
        KbLog("KB-LATCH-SELFTEST: fresh 2000 ms streak: AUTO=%d (expect 1 - latched again)",
              gAutoOn ? 1 : 0);
        break;
    case 27: AutoSet(false, "self-test streak exercise", false);
        LatchFrame(false);
        gLatchSimT += 900; LatchFrame(false);   // serve the beat again
        gStreakStart = 0; gLastCastT = 0;       // clean slate for the gap test
        gLatchSimT += 100; LatchNoteCast(gLatchSimT);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        gLatchSimT += 700; LatchNoteCast(gLatchSimT);   // 700 ms gap: span restarts
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        KbLog("KB-LATCH-SELFTEST: gap-broken span at 1200 ms: AUTO=%d (expect 0)",
              gAutoOn ? 1 : 0);
        break;
    case 28: gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);   // restarted span hits 2000 ms
        KbLog("KB-LATCH-SELFTEST: restarted span reaches 2000 ms: AUTO=%d (expect 1)",
              gAutoOn ? 1 : 0);
        AutoSet(false, "self-test streak exercise", false);
        gLatchSimT = 0;
        gSelfWorldSim = false;
        gSelfForced = false;
        KbLog("KB-LATCH-SELFTEST: segment complete (AUTO on=%d, expect 0)", gAutoOn ? 1 : 0);
        break;
    case 29: KbLog("KB-CLOBBER-SELFTEST: segment starts - physical RMB hold simulated (RMB-law hold: its casts must never latch)");
        gSelfForced = true;
        gSelfWorldSim = true;
        gLatchSimT = 700000;
        gLatchSuppress = false; gStreakStart = 0; gLastCastT = 0;   // clean slate
        Platform::InjectVirtualKey(2, true);    // physical RMB goes down
        DAT_083a42ac = 1;                        // WndProc state from that real press
        KbLog("KB-CLOBBER-SELFTEST: RMB held=%d shared-flag=%d (expect 1/1)",
              KeyHeld(2) ? 1 : 0, (int)DAT_083a42ac);
        break;
    case 30: for (int k = 0; k < 6; ++k) { LatchNoteCast(gLatchSimT, KeyHeld(2)); gLatchSimT += 400; }
        // casts at T..T+2000 under the held RMB, fed through the same
        // mouse-aware entry the real tick uses: the RMB law (2026-11-16)
        // says the span can never complete — AUTO must stay off.
        KbLog("KB-CLOBBER-SELFTEST: 2 s of casts under a held RMB: AUTO=%d RMB held=%d (expect 0/1 - no latch from mouse casting)",
              gAutoOn ? 1 : 0, KeyHeld(2) ? 1 : 0);
        AutoSet(true, "self-test clobber exercise", false);   // arm AUTO directly (the F8 path) — the clobber scenario below needs a running drive, not a latch
        AutoDriveButton();   // the drive's first button step
        KbLog("KB-CLOBBER-SELFTEST: drive button step: synth-held=%d shared-flag=%d (expect 1/1)",
              gRightDown ? 1 : 0, (int)DAT_083a42ac);
        break;
    case 31: Platform::InjectVirtualKey(2, false);   // player lets go of the mouse
        DAT_083a42ac = 0;   // the real RightUp clears the shared flag (Input_WndProc)
        KbLog("KB-CLOBBER-SELFTEST: RMB released: held=%d AUTO=%d (expect 0/1 - the pre-existing hold's release is not an off input)",
              KeyHeld(2) ? 1 : 0, gAutoOn ? 1 : 0);
        AutoDriveButton();   // the drive must re-assert the clobbered hold
        KbLog("KB-CLOBBER-SELFTEST: after drive step: shared-flag=%d synth-held=%d (expect 1/1 - hold re-asserted, AUTO keeps casting)",
              (int)DAT_083a42ac, gRightDown ? 1 : 0);
        break;
    case 32: KbLog("KB-CLOBBER-SELFTEST: injecting a NEW physical RMB press - AUTO must cancel (standing rule)");
        Platform::InjectVirtualKey(2, true);
        DAT_083a42ac = 1;   // WndProc state from the real press (already 1 via the synth hold)
        break;
    case 33: KbLog("KB-CLOBBER-SELFTEST: after new press: AUTO=%d (expect 0 - cancelled by fresh mouse input)",
              gAutoOn ? 1 : 0);
        Platform::InjectVirtualKey(2, false);
        gLatchSimT = 0;
        gSelfWorldSim = false;
        gSelfForced = false;
        KbLog("KB-CLOBBER-SELFTEST: segment complete (AUTO on=%d, expect 0)", gAutoOn ? 1 : 0);
        break;
    case 34: KbLog("KB-DECOUPLE-SELFTEST: segment starts - AUTO with keyboard controls OFF (KB on=%d, AUTO on=%d, expect AUTO 0)",
               gKbEnabled ? 1 : 0, gAutoOn ? 1 : 0);
        gSelfForced = true;
        gSelfWorldSim = true;
        if (gKbEnabled) KbSet(false, "self-test decouple exercise");
        KbLog("KB-DECOUPLE-SELFTEST: KB on=%d (expect 0). Injecting F8 down - AUTO must engage with KB off",
              gKbEnabled ? 1 : 0);
        Platform::InjectVirtualKey(kAutoVk, true);
        break;
    case 35: Platform::InjectVirtualKey(kAutoVk, false);
        KbLog("KB-DECOUPLE-SELFTEST: after F8: AUTO on=%d (expect 1 - engaged with KB off)",
              gAutoOn ? 1 : 0);
        AutoSet(false, "self-test decouple exercise", false);
        LatchFrame(false);   // the off transition engages the anti-re-latch beat...
        gLatchSimT = 900000;
        gLatchSimT += 900; LatchFrame(false);   // ...which a 900 ms silence then serves
        gLatchSuppress = false; gStreakStart = 0; gLastCastT = 0;   // clean slate regardless
        LatchNoteCast(gLatchSimT);            // streak opens with KB off
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        KbLog("KB-DECOUPLE-SELFTEST: 3 casts over 800 ms with KB off: streaking=%d AUTO=%d (expect 1/0)",
              gStreakStart != 0 ? 1 : 0, gAutoOn ? 1 : 0);
        break;
    case 36: gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT);   // span reaches 2000 ms
        KbLog("KB-DECOUPLE-SELFTEST: span 2000 ms with KB off: AUTO=%d (expect 1 - streak latched with KB off)",
              gAutoOn ? 1 : 0);
        KbSet(true, "self-test decouple exercise");    // flip ON mid-AUTO
        KbLog("KB-DECOUPLE-SELFTEST: F9 flip ON mid-AUTO: KB on=%d AUTO on=%d (expect 1/1)",
              gKbEnabled ? 1 : 0, gAutoOn ? 1 : 0);
        KbSet(false, "self-test decouple exercise");   // flip OFF mid-AUTO
        KbLog("KB-DECOUPLE-SELFTEST: F9 flip OFF mid-AUTO: KB on=%d AUTO on=%d (expect 0/1)",
              gKbEnabled ? 1 : 0, gAutoOn ? 1 : 0);
        break;
    case 37: AutoSet(false, "self-test decouple exercise", false);
        KbSet(true, "self-test decouple restore");
        gLatchSimT = 0;
        gSelfWorldSim = false;
        gSelfForced = false;
        KbLog("KB-DECOUPLE-SELFTEST: segment complete (KB on=%d expect 1, AUTO on=%d expect 0)",
              gKbEnabled ? 1 : 0, gAutoOn ? 1 : 0);
        break;
    default: break;
    }
}

} // namespace

// The master state, for the Space pickup handler in Player_InputTick.cpp.
bool KbPlay_Enabled() { return KbEnabled(); }

// Tab hook for HUD_HotkeyTick: cycle the combat lock among attackable
// monsters in range (nearest first, wrapping). True = Tab consumed.
// With the master switch OFF this returns false at once, so Tab keeps
// its stock minimap toggle.
bool KbPlay_TabCycle()
{
    if (!Gates() || !KbEnabled()) return false;
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
        gKbEnabled = gUserSettings.GetKeyboardEnable();
        KbLog("KB: module init%s - keyboard controls %s (Config.ini [Keyboard] Enable; F9 toggles)",
              gSelfTest ? " (self-test)" : "", gKbEnabled ? "ON" : "OFF");
    }
    if (gSelfTest) SelfTestDrive();

    // Finish the Enter NPC click started last tick.
    if (gNpcClickUp) {
        gNpcClickUp = false;
        SendMouse(Platform::InputEventType::LeftUp, gCurX, gCurY);
    }

    // The master switch folds into the shared gates: with KB OFF a held
    // cast stops, and no digit/Enter press is ever acted on.
    const bool gates = Gates() && KbEnabled();

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
    // Attacking itself is only the 0/1 attack-state flag (globals.h) —
    // the engaged target's character index rides in DAT_00559c70, which
    // Player_InputTick writes next to it (= SelectedCharacter on engage,
    // -1 when the target dies). Reading Attacking as an index fed 0/1
    // here, which ValidMonster essentially never accepts, so the mouse
    // path's target preference silently never landed and AUTO fell back
    // to the nearest scan instead of the player's current target.
    if (AutoGates() && Attacking == 1 && ValidMonster(DAT_00559c70))
        gLastIdx = DAT_00559c70;
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
            else if (!KbEnabled())
                KbLog("KB: digit %d press ignored - keyboard controls OFF (F9 re-enables)", kDigitVal[i]);
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
    // F8 toggles AUTO regardless of the keyboard master switch (MEAMU
    // decouple, 2026-11-15): AUTO is a cast drive bounded by AutoGates(),
    // not a keyboard feature. Chat focus still owns the key while typing.
    // (The gSelfWorldSim allowance on the engage path is title-screen
    // test scaffolding only, mirroring the F9 block below; production
    // behavior is AutoGates() alone.)
    const bool f8 = KeyHeld(kAutoVk);
    if (f8 && !gF8Prev) {
        if (chat) {
            if (gSelfTest)
                KbLog("KB-AUTO-SELFTEST: F8 pressed while chat focused - no toggle (key belongs to chat)");
        } else if (gAutoOn) {
            AutoSet(false, "toggle key", true);
        } else if ((AutoGates() || (gSelfTest && gSelfWorldSim)) && !HeroDead()) {
            AutoSet(true, "toggle key", true);
        } else if (gSelfTest) {
            KbLog("KB-AUTO-SELFTEST: F8 toggle ignored (autogates=%d dead=%d scene=%d)",
                  AutoGates() ? 1 : 0, HeroDead() ? 1 : 0, SceneFlag);
        }
    }
    gF8Prev = f8;

    // ── Keyboard controls master toggle (F9) ─────────────────────────────
    // Same discipline as the F8 toggle: the press edge counts only with
    // the chat input closed (while chat is focused the key belongs to
    // chat) and the world gates open (AutoGates: in-world scene, hero
    // present, no error modal). The flip is immediate and persists.
    const bool f9 = KeyHeld(kToggleVk);
    if (f9 && !gF9Prev) {
        if (chat) {
            if (gSelfTest)
                KbLog("KB-SELFTEST: F9 pressed while chat focused - no toggle (key belongs to chat)");
        } else if (AutoGates() || (gSelfTest && gSelfWorldSim)) {
            KbSet(!gKbEnabled, "toggle key");
        } else if (gSelfTest) {
            KbLog("KB-SELFTEST: F9 toggle ignored (autogates=%d scene=%d)",
                  AutoGates() ? 1 : 0, SceneFlag);
        }
    }
    gF9Prev = f9;

    // A physical mouse press is the player taking control back. The key
    // table's button bits are written only by real SDL device events (pad
    // key pulses never use buttons 1/2); virtual buttons from this module,
    // NavMove and the gamepad travel via DeliverSDLInput and never appear
    // here, so this cannot misfire on synthesized input.
    const bool mL = KeyHeld(1), mR = KeyHeld(2);
    if (gAutoOn && ((mL && !gMouseLPrev) || (mR && !gMouseRPrev)))
        AutoSet(false, "manual mouse input", true);
    gMouseLPrev = mL; gMouseRPrev = mR;

    // Cast-streak latch: observe casts + run the anti-re-latch beat.
    // Placed before the AUTO drive so a latch that completes starts
    // driving this same tick.
    LatchFrame(chat);

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
            if (SelectedSkillType() != 0) { AimAndPark(); AutoDriveButton(); }
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
