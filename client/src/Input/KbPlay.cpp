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
//   AUTO ends on: F8 again (chat closed), a manual recast of the very
//   skill AUTO is casting (Train 12 OFF rule, below), an error modal,
//   leaving the world scene, or the hero's death. A hand-held digit
//   cast and the gamepad both outrank AUTO while they drive; AUTO
//   resumes when they stop.
//   CURSOR FREEDOM (MEAMU Train 12, G's law 2026-11-19): the cursor
//   stays the player's while AUTO runs. The old drive parked the
//   virtual cursor on the aim target EVERY tick and kept the virtual
//   right button permanently held, so the player's physical cursor
//   was stomped back onto the target each frame and any click-to-move
//   was overridden by the held button's attack-walk on the next
//   tick — the mouse was unusable with AUTO on, and a fresh physical
//   press was treated as a takeover that cancelled AUTO. All of that
//   is gone: a physical mouse press no longer cancels AUTO, the
//   drive parks only when its aim target changes or the target's
//   screen position drifts materially, it never parks while the
//   player is moving the mouse or holding a physical button, and it
//   asserts its virtual hold only while the cursor actually sits on
//   the aim — the player's input wins every moment it is given, and
//   AUTO's steering merely fills the gaps (see AutoDriveStep).
//
// * Cast-streak AUTO trigger (MEAMU, G's ask 2026-11-13; REWORKED by
//   Train 12, G's design locked 2026-11-19): casting the SAME
//   offensive skill continuously for 5.0 s latches AUTO on WITH that
//   skill, through the same AutoSet path as F8 (same banner +
//   indicator, same gAutoOn state — no parallel machine).
//   "Continuously" is cycle-relative: a gap between two casts of the
//   tracked skill breaks the span only when it exceeds ~1.5x the
//   skill's own cycle as this hero actually casts it — the observed
//   interval between the span's casts, clamped to [0.8 s, 3.0 s],
//   with 1.5 s before the first interval is measured.
//   GetSkillInformation exposes only mana, distance and AG in this
//   fork (no delay accessor exists), and the measured interval
//   already includes the hero's attack speed, which a static table
//   delay would not. A cast of a different offensive skill restarts
//   the span under the new skill. Self/buff/mode skills (IsSelfSkill:
//   heals, buffs, the 58-76 mode toggles) are invisible to the
//   tracker: they never build, break or count.
//   The trigger is the cast itself, not any one input: every source
//   funnels into Combat_DispatchHeroSkillAttack, whose branches
//   stamp SkillStage = 1 at the cast-commit point; the tick polls
//   that stamp for rising edges and reads the cast skill from the
//   hero's CurrentSkill selection at the edge (every cast source —
//   digit, pad mirror, skill-bar click — selects before it casts).
//   TWO sources are excluded from BUILDING a span since 2026-11-16
//   (RMB law + pad law, below): casts made while the physical right
//   mouse button is held, and casts driven by a held gamepad skill
//   button. The span builds only while AUTO is off, chat is closed
//   and the world gates pass — the keyboard master switch does NOT
//   gate it (decouple, 2026-11-15); chat, a scene change, an error
//   modal or the hero's death resets it.
//   While AUTO is ON, the same cast edges run G's OFF/SWITCH rules
//   (Train 12): a MANUAL cast — digit cast-hold, pad skill button or
//   a physical mouse button, anything but AUTO's own drive — of the
//   skill AUTO is casting turns AUTO OFF, and that manual cast still
//   fires: control simply returns to the player's hand. A manual
//   cast of a DIFFERENT offensive skill keeps AUTO on and switches
//   AUTO's skill to it. AUTO's own casts are recognised by the
//   absence of every manual source flag (no digit cast-hold, no pad
//   skill button, no physical mouse button) and can never cancel or
//   switch themselves. Self-skill casts are invisible here too:
//   buffing or toggling a mode mid-AUTO changes nothing.
//   Anti-re-latch: when AUTO turns off (say the OFF rule fires
//   mid-hold), no new span can arm until casting has fully stopped
//   for ~0.8 s, so the same hold that was interrupted can never
//   instantly re-latch.
//   RMB law (MEAMU 2026-11-16, G's order): the mouse is OUT of the
//   span — a cast observed while the physical right button is held
//   stamps the beat clock but kills the span, so holding RMB can
//   never latch AUTO. Stock hold-to-attack (the Combat gate on the
//   shared MouseRButton flag) is a different path and is untouched;
//   digit holds and clicks streak exactly as before. (While AUTO is
//   ON, an RMB-held cast is a manual cast and the OFF/SWITCH rules
//   above apply to it — the law only ever governed latching.)
//   Pad law (MEAMU 2026-11-16, G's order): gamepad skill-button casts
//   are OUT of the span too — a cast observed while any pad skill
//   button is held (Gamepad_PadCastHeld: A/X/Y/B/RB/LB/RT/LT/L3/R3,
//   all digit mirrors since PADCAST) stamps the beat clock but kills
//   the span, so holding a pad button to repeat-cast can never latch
//   AUTO, and casting still stops the moment the button is released.
//   STATED LIMITATION (Train 12): a pad hold therefore cannot ARM
//   AUTO either — PADCAST holds stay pure manual casts by G's
//   standing order; the 5 s trigger arms from keyboard/mouse casting.
//   Real-keyboard digit holds are untouched and latch as before.
//
// * Master toggle (MEAMU kb-toggle, 2026-11-13): Config.ini [Keyboard]
//   Enable (default 1 when missing) decides the boot state; F9 flips it
//   in game under the AUTO toggle's gate discipline and persists the new
//   value. OFF folds into the shared Gates() so digits/Tab/Enter (and
//   the Space pickup upgrade) revert to stock behavior at once. The
//   Space ATTACK LATCH is not part of the switch either (MEAMU
//   Train 9 decouple, 2026-11-19 - see KbPlay_SpaceTap). AUTO is
//   NOT part of the
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
#include "Input/ActionMove.h"    // Train 13: scheme gate + blink-engage hook
#include "Platform/IInput.h"     // Platform::InputEvent, GetInput
#include "Core/Window.h"         // gWindow
#include "Entity/EntityTypes.h"  // CHARACTER, CHARACTER_STRIDE
#include "Entity/EntityName.h"   // EntityName::Get
#include "Net/Recv/FabricatedMark.h" // FabricatedMark (Train 10: Space latch vs fabricated bodies)
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
constexpr Uint64 kLatchSpanMs  = 5000; // same-skill casting that latches AUTO (Train 12; was 2000)
constexpr Uint64 kTrigGapDefMs = 1500; // gap tolerance before the first cycle is measured
constexpr Uint64 kTrigGapMinMs = 800;  // gap tolerance floor (fast skills, jitter)
constexpr Uint64 kTrigGapMaxMs = 3000; // gap tolerance ceiling (slow skills)
constexpr Uint64 kLatchRearmMs = 800;  // cast silence required after AUTO off
constexpr Uint64 kCursorIdleMs = 600;  // cursor stillness that reads as a gap AUTO may fill
constexpr int kParkDriftPx     = 14;   // aim drift (UI px) that earns a re-park
constexpr int kCursorAimPx     = 12;   // cursor within this of the aim = sitting on it

// ── State ────────────────────────────────────────────────────────────────────
bool gDigitPrev[10] = {};        // raw held state last frame, per digit
int  gCastVk        = 0;         // digit vk currently cast-holding (0 = none)
bool gRightDown     = false;     // this module's virtual RMB state
bool gYielded       = false;     // cast held but pad owns the right button
int  gLockIdx       = -1;        // Tab-locked CharactersClient index
int  gAimIdx        = -1;        // current cast aim (CharactersClient index)
int  gLastIdx       = -1;        // hero's last valid attack target
int  gAtkLatchIdx   = -1;        // Space attack latch: locked target index
int  gAtkLatchKey   = -1;        // its network Key at lock time (slot guard)
bool gAtkLeftDown   = false;     // the latch's virtual left-button state
bool gAtkYielded    = false;     // latch on, digit cast / pad owns the buttons
bool gNpcClickUp    = false;     // Enter NPC click: LeftUp due next tick
bool gEnterPrev     = false;
float gCurX = 0.0f, gCurY = 0.0f;   // last parked cursor (window px)
bool gAutoOn        = false;     // AUTO mode (F8) master state
int  gAutoSkill     = 0;         // skill AUTO is committed to (Train 12 OFF/SWITCH reference)
bool gF8Prev        = false;     // toggle key last frame
bool gKbEnabled     = true;      // keyboard controls master switch
                                 // ([Keyboard] Enable at boot, F9 in game)
bool gF9Prev        = false;     // master toggle key last frame
bool gMouseLPrev    = false;     // physical mouse buttons last frame
bool gMouseRPrev    = false;
bool gAutoYielded   = false;     // AUTO on, gamepad owns the right button
Uint64 gStreakStart = 0;         // cast-streak latch: first cast of the span
Uint64 gLastCastT   = 0;         // cast-streak latch: most recent cast stamp
int    gTrigSkill   = 0;         // Train 12 trigger: skill the current span builds on
Uint64 gTrigLastT   = 0;         // Train 12 trigger: last counted cast of gTrigSkill
Uint64 gTrigGapMs   = kTrigGapDefMs; // Train 12 trigger: live gap tolerance (1.5x measured cycle)
int    gParkUiX     = -1;        // last synthetic park point, UI space (640x480)
int    gParkUiY     = -1;
int    gMouseUiX    = -1;        // game cursor pos at the end of last tick (UI space)
int    gMouseUiY    = -1;
Uint64 gCursorChangeT = 0;       // last time the game cursor position changed
int    gAutoParkIdx = -2;        // aim index the AUTO drive last parked on (-2 = none)
BYTE gSkillStagePrev = 0;        // hero SkillStage last tick (edge detect)
bool gLatchSuppress = false;     // anti-re-latch: waiting out the silence beat
Uint64 gSuppressT   = 0;         // when the current suppression engaged
bool gAutoPrevLatch = false;     // AUTO state last tick (off-transition watch)
Uint64 gLatchSimT   = 0;         // self-test: simulated latch clock (0 = real)
bool gChatPrev      = false;     // chat focus last frame (transition logs)

// ── Self-test state ──────────────────────────────────────────────────────────
constexpr int kTestSkillA = 41;   // self-test offensive skill (not in IsSelfSkill)
constexpr int kTestSkillB = 42;   // self-test second offensive skill
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

// Project a character's world position to 640x480 UI space (the
// space MouseX/MouseY — DAT_083a427c/278 — live in).
void ProjectUi(CHARACTER* c, int& ux, int& uy)
{
    ux = 0; uy = 0;
    Camera_ProjectWorldToScreen((float*)c->Object.Position, &ux, &uy);
}

// Project a character's world position to window pixels (the projection
// returns 640x480 UI space; DeliverSDLInput takes real window pixels).
// The UI-space point is recorded in gParkUiX/Y: the drives compare the
// live game cursor and the aim's current projection against it to
// steer without seizing the player's cursor (Train 12).
void ParkOnCharacter(CHARACTER* c)
{
    int sx = 0, sy = 0;
    ProjectUi(c, sx, sy);
    gParkUiX = sx; gParkUiY = sy;
    const float winW = (float)gWindow.GetRealWidth();
    const float winH = (float)gWindow.GetRealHeight();
    ParkCursor(sx * winW / 640.0f, sy * winH / 480.0f);
}

// Chebyshev distance between two UI-space points (the module's tile
// metric, at screen scale).
int UiDist(int ax, int ay, int bx, int by)
{
    const int dx = std::abs(ax - bx), dy = std::abs(ay - by);
    return dx > dy ? dx : dy;
}

// True when the game cursor has moved since the end of last tick —
// i.e. the player (or another input path) is moving the mouse right
// now. Synthetic parks land in MouseX/MouseY synchronously
// (DeliverSDLInput calls the input sink directly), and the tick
// refreshes gMouseUiX/Y after every drive, so our own parks never
// read as player movement.
bool PlayerMovingMouse()
{
    return (int)DAT_083a427c != gMouseUiX || (int)DAT_083a4278 != gMouseUiY;
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

// Latch/AUTO-aim legality (MEAMU Train 10 + Bounce 11; AUTO aim
// adopted it in Train 12): ValidMonster, plus
// fabricated player-kind bodies the GS flagged through F3:FC (see
// Net/Recv/FabricatedMark.h) — phantoms/KAGE, companions, roamers,
// ambushers, dancers, and other players' clan summons/tames. The
// hero's OWN followers are excluded (a latch that eats your own army
// is a bug, not a feature); real players are excluded by construction
// — they are never flagged, so they stay manual-only targets. The
// stock click-attack pipeline still decides whether a swing actually
// goes out (CheckAttack: a rival-presented body passes at PK >= 6,
// exactly as for the mouse), and the server enforces the rest.
bool ValidLatchTarget(int idx)
{
    if (ValidMonster(idx)) return true;
    if (idx < 0 || idx >= 400 || !Hero) return false;
    CHARACTER* c = EntryAt(idx);
    if ((char*)c == (char*)(uintptr_t)Hero) return false;
    if (c->Object.Live == 0) return false;
    if (c->Dead != 0) return false;
    if (!FabricatedMark::Is(c)) return false;
    if (FabricatedMark::Kind(c) == FabricatedMark::kFollower &&
        FabricatedMark::Owner(c) == (int)HeroC()->Key) return false;
    return true;
}

// Acquire the aim target for a cast: Tab lock, then last target, then
// nearest (tile distance, facing-arc tie-break) within acquisition
// range. Train 12: the scan and the last-target preference use
// ValidLatchTarget — monsters plus GS-flagged fabricated bodies,
// minus the hero's own followers — so a digit hold or AUTO can hunt
// in the Stadium exactly like the Space latch (Train 10 widened only
// the latch; G's Train 12 spec defines AUTO's acquisition by the
// same test). The Tab lock itself still only ever holds a monster
// (Tab cycling is unchanged), so its check stays ValidMonster.
int AcquireTarget(int skillDist)
{
    CHARACTER* hero = HeroC();
    int acq = skillDist + 8;
    if (acq < 8) acq = 8;
    if (acq > 14) acq = 14;

    if (ValidMonster(gLockIdx) && Cheb(hero, EntryAt(gLockIdx)) <= acq + 2)
        return gLockIdx;
    if (ValidLatchTarget(gLastIdx) && Cheb(hero, EntryAt(gLastIdx)) <= acq)
        return gLastIdx;

    int   best = -1, bestDist = 0;
    float bestArc = 0.0f;
    for (int i = 0; i < 400; ++i) {
        if (!ValidLatchTarget(i)) continue;
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
    return skillType == 26 || skillType == 27 || skillType == 28 || skillType == 48 || skillType == 57 || skillType == 58 || skillType == 59 || skillType == 60 || skillType == 61 || skillType == 72 || skillType == 73 || skillType == 74 || skillType == 75 || skillType == 76;  // 57 = Omni Buff (MEAMU 2026-11-14), 58 = Mangekyou Sharingan (MEAMU 2026-11-15), 59 = Sage Mode (MEAMU 2026-11-17), 60 = Jinchuriki Mode + 61 = Biju Mode (MEAMU 2026-11-17, Train 4), 72 = Biju Bond + 73 = Chakra Cloak + 74 = Perfect Susanoo + 75 = Dragon Sage Mode (MEAMU 2026-11-18), 76 = Biju Legion (MEAMU 2026-11-18, Train 6 prep)
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

// Maintain the shared cast aim and return its table index (-1 = the
// hero: self skill selected, or no target in acquisition range).
int ComputeAim()
{
    CHARACTER* hero = HeroC();
    if (IsSelfSkill(SelectedSkillType())) {
        gAimIdx = -1;
        return -1;
    }
    int t = gAimIdx;
    if (!ValidLatchTarget(t) || Cheb(hero, EntryAt(t)) > SelectedSkillDist() + 16)
        t = AcquireTarget(SelectedSkillDist());
    gAimIdx = t;
    return t;
}

// ── Locked walk-in (MEAMU Train 20) ─────────────────────────────────────
// Train 17's root cause, second family: while the Action Camera is
// pointer-locked, the park that steers this module's drives is
// swallowed (Input_OnEvent eats MouseMove as a camera delta) and the
// cursor globals stay pinned to screen centre — so the held button's
// stock pipeline walks toward the RETICLE hover (SelectedCharacter),
// never toward this module's own aim, and its gap-closing is inert.
// Fix, mirroring Train 17: while locked, an out-of-reach aim gets a
// direct route to its own tile (ActionMove_DirectRouteTo). The step
// stands down when the reticle already sits on the aim (the stock
// click-attack branch routes to that target's tile itself) and when
// the aim is inside reach. Re-issue mirrors hold-to-move: on target
// change, or on the 400 ms refresh (NavMove's follow cadence) — the
// target may have moved, or the route run out with the aim still
// out of reach. Reachability: digit holds and AUTO drive in ACTION
// directly; the Space latch engages CLASSIC-only (KbPlay_SpaceTap)
// but survives a grave flip into ACTION — nothing releases it on
// the flip and its drive has no scheme gate — so it is covered too
// (reach 1, a basic attack).
int      gLockWalkIdx = -1;   // aim the locked walk-in last routed toward
unsigned gLockWalkT   = 0;    // last issue / attempt stamp (SDL ticks)
constexpr unsigned kLockWalkRefreshMs = 400;

void LockedWalkInStep(int targetIdx, int reachTiles)
{
    if (targetIdx < 0) return;
    if (!ActionMove_CameraLocked()) return;   // not locked: the park rules
    CHARACTER* hero = HeroC();
    CHARACTER* tgt  = EntryAt(targetIdx);
    if (Cheb(hero, tgt) <= reachTiles) return;
    if (SelectedCharacter == targetIdx) return;   // pipeline already on it
    const unsigned now = (unsigned)SDL_GetTicks();
    if (targetIdx == gLockWalkIdx &&
        (int)(now - gLockWalkT) < (int)kLockWalkRefreshMs)
        return;
    gLockWalkIdx = targetIdx;
    gLockWalkT   = now;
    ActionMove_DirectRouteTo((int)tgt->PositionX, (int)tgt->PositionY);
}

void AimAndPark()
{
    CHARACTER* hero = HeroC();
    const int t = ComputeAim();
    // MEAMU Train 13 (blink-engage): a valid aim target beyond the skill's
    // reach gets ONE blink request per engagement instead of relying on
    // the combat walk-in alone; if the server refuses or never answers,
    // the park + hold below walks in exactly as before. This is AUTO's
    // gap-closer too (the AUTO drive aims through here every tick, and
    // the helper's guard throttles it to a single attempt).
    if (t >= 0 && Cheb(hero, EntryAt(t)) > SelectedSkillDist())
        ActionMove_TryBlinkEngage(t, SelectedSkillDist());
    // MEAMU Train 31 (G's law: Action = manual movement only): in the
    // Action scheme the aim NEVER moves him — no locked walk-in, and
    // the park lands on the hero himself, so AUTO and skill-holds cast
    // in place at aim. The blink above is the only closer, and it is a
    // teleport he triggers by attacking. Classic keeps the walk-in.
    if (ActionMove_IsAction()) {
        ParkOnCharacter(hero);
        return;
    }
    // MEAMU Train 20: while pointer-locked the park below is swallowed
    // and the held button's pipeline would walk toward the reticle
    // hover instead — close on the aim by direct route.
    LockedWalkInStep(t, SelectedSkillDist());
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
    // Train 12: snapshot the skill AUTO commits to (the OFF/SWITCH
    // reference). The streak trigger re-stamps it with the tracked
    // skill right after engaging.
    if (on) gAutoSkill = SelectedSkillType();
    if (!on) gAutoParkIdx = -2;   // the next engage parks fresh
    // A hand-held digit cast owns the shared right button otherwise; its
    // maintenance block keeps it held without help from here. If the
    // player is physically holding RMB, the shared flag is theirs:
    // forget our hold instead of sending a RightUp that would clear
    // the flag under their press (the AutoDriveButton clobber, from
    // the other side).
    if (!on && !gCastVk) {
        if (KeyHeld(2)) gRightDown = false;
        else SetRightButton(false);
    }
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

// ── AUTO drive step (Train 12 cursor freedom) ────────────────────────────────
// The drive's per-tick step, replacing the old unconditional
// AimAndPark() + AutoDriveButton(). The old code parked the virtual
// cursor on the aim target EVERY tick: the synthetic MouseMove lands
// in the game's MouseX/MouseY synchronously, so the player's physical
// cursor was overwritten every frame, and the permanently-held
// virtual right button turned that parked point into a continuous
// attack/move command that overrode any click-to-move on the next
// tick. The cursor is the player's now; this step only fills gaps:
//
// * A physical button is held -> the moment is the player's: no park,
//   no synthetic hold. Ours is released — or simply forgotten when
//   the physical RMB owns the shared flag, so no synthetic RightUp
//   clears the flag under their press.
// * The cursor already sits on the aim (our park still lines up, or
//   the player is hovering the target themselves) -> hold, no park.
// * The aim changed / drifted materially, or the cursor has sat
//   still off the aim for kCursorIdleMs, and the player is not
//   moving the mouse this tick -> park once on the aim and hold.
// * Anything else (player mid-move, cursor freshly elsewhere) ->
//   release the hold and idle the steering. AUTO stays ON; when the
//   player click-moves away, the hero completes the move and the
//   drive re-acquires from the new position by the normal rule.
void AutoDriveStep()
{
    CHARACTER* hero = HeroC();
    const int t = ComputeAim();
    CHARACTER* aimC = t >= 0 ? EntryAt(t) : hero;
    int ux = 0, uy = 0;
    ProjectUi(aimC, ux, uy);

    if (KeyHeld(1) || KeyHeld(2)) {
        if (gRightDown) {
            if (KeyHeld(2)) gRightDown = false;   // physical RMB owns the shared flag
            else SetRightButton(false);
        }
        gAutoParkIdx = -2;   // park fresh when the drive resumes
        return;
    }
    // MEAMU Train 31 (G's law: Action = manual movement only): AUTO
    // never steers him in the Action scheme — no locked walk-in, and
    // the park below lands on the hero (cast in place at aim), never
    // on the aim character. Classic keeps the full steering.
    const bool actionManual = ActionMove_IsAction();
    // MEAMU Train 20: while pointer-locked, close on the aim by direct
    // route — the park-and-hold steering below reads the pinned centre
    // cursor and can only ever walk toward the reticle hover. (Placed
    // after the physical-button yield above: a held physical button is
    // the player's moment, and no steering of any kind fills it.)
    if (!actionManual) LockedWalkInStep(t, SelectedSkillDist());
    const int cx = (int)DAT_083a427c, cy = (int)DAT_083a4278;   // MouseX / MouseY
    if (UiDist(cx, cy, ux, uy) <= kCursorAimPx) {
        AutoDriveButton();   // aim is under the cursor: hold, no park
        return;
    }
    const bool aimMoved = t != gAutoParkIdx || gAutoParkIdx == -2 ||
                          UiDist(ux, uy, gParkUiX, gParkUiY) > kParkDriftPx;
    const bool cursorIdle = (SDL_GetTicks() - gCursorChangeT) >= kCursorIdleMs;
    if (!PlayerMovingMouse() && (aimMoved || cursorIdle)) {
        ParkOnCharacter(actionManual ? hero : aimC);   // Train 31
        gAutoParkIdx = t;
        AutoDriveButton();
        return;
    }
    if (gRightDown) SetRightButton(false);   // cursor is the player's: steer nothing
}

// ── Space attack latch (MEAMU Train 8 addendum, G's ruling 2026-11-19) ──────
// G's model, ordered explicitly as a KEYBOARD-ONLY exception to the pad
// no-latch law (PADCAST still never latches; F8/F9 AUTO is untouched):
// TAP Space -> lock the closest attackable enemy -> the hero basic-attacks
// it continuously WITHOUT holding anything -> the latch releases the
// moment that target dies (hero stops; the next tap locks the next one).
// A tap while latched cancels. Despawn / pool-slot recycling releases too.
//
// A basic attack has no skill, so the latch drives the LEFT button — the
// stock click-attack pipeline (Player_InputTick: v32 = MouseLButtonPush ||
// MouseLButton -> hover -> attack stage -> basic-attack packet, walk-in
// included) — exactly a manual click-attack held down, parked on the
// locked target under the same cursor-freedom discipline as the AUTO
// drive (Train 12: park on drift only, hold only while the cursor
// sits on the target). Target legality is ValidLatchTarget (Train
// 10); the server enforces the rest (own summons, safe zones) exactly
// as it does for mouse attacks.
//
// MEAMU Train 9 widen (G's order, 2026-11-19): acquisition is NO longer
// capped at 12 tiles - the scan covers the whole client character
// table, i.e. every attackable entity on the visible screen (the table
// holds exactly the entities the client has in view), and the
// click-attack pipeline's walk-in closes the distance to a far lock.
//
// MEAMU Train 10 (2026-11-19): legality is ValidLatchTarget — ValidMonster
// plus the fabricated player-kind bodies the Bounce 11 GS flags through
// F3:FC (phantoms/KAGE/companions/roamers/ambushers/dancers and other
// players' followers), minus the hero's own followers. Train 10 widened
// only the Space latch, so Space could hunt in the Stadium; Train 12
// adopted the same test for AUTO aim and digit-hold aim (AcquireTarget),
// so AUTO hunts there too. Tab cycling keeps plain ValidMonster. Real
// players are never flagged and stay manual-only.

void SetLeftButton(bool down)
{
    if (down == gAtkLeftDown) return;
    gAtkLeftDown = down;
    SendMouse(down ? Platform::InputEventType::LeftDown
                   : Platform::InputEventType::LeftUp, gCurX, gCurY);
}

// Clobber repair, mirroring AutoDriveButton: the shared MouseLButton flag
// (DAT_083a42c4) is zeroed by Player_InputTick whenever it decides the
// user is not clicking, so a fresh virtual LeftDown re-asserts the hold.
void AtkDriveButton()
{
    if (DAT_083a42c4 == 0) gAtkLeftDown = false;
    SetLeftButton(true);
}

// Closest-first acquisition (the AcquireTarget scan with the spec's
// order: no Tab-lock / last-target preference — G's spec locks the
// closest attackable enemy). Train 9: the scan is screen-wide -
// every entry of the client character table (the viewport list)
// competes; closest = smallest Chebyshev tile distance, the
// facing arc breaks ties.
int AcquireNearestBasic()
{
    CHARACTER* hero = HeroC();
    int   best = -1, bestDist = 0;
    float bestArc = 0.0f;
    for (int i = 0; i < 400; ++i) {
        if (!ValidLatchTarget(i)) continue;
        CHARACTER* c = EntryAt(i);
        const int d = Cheb(hero, c);
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

void AtkLatchRelease(const char* why, bool announce)
{
    if (gAtkLatchIdx < 0) return;
    gAtkLatchIdx = -1;
    gAtkLatchKey = -1;
    gAtkYielded  = false;
    SetLeftButton(false);
    if (announce && SceneFlag == 5) Say("Attack latch OFF");
    KbLog("KB-ATK: latch off (%s)", why);
}

void AtkLatchEngage(int idx)
{
    // One cursor, one drive: the latch takes over from AUTO (and the
    // tick drops the latch the moment AUTO engages instead).
    if (gAutoOn) AutoSet(false, "space attack latch takes the drive", false);
    gAtkLatchIdx = idx;
    gAtkLatchKey = (int)EntryAt(idx)->Key;
    gAtkYielded  = false;
    ParkOnCharacter(EntryAt(idx));
    AtkDriveButton();
    Say("Target locked: %s", EntityName::Get((char*)EntryAt(idx)));
    KbLog("KB-ATK: latch on idx %d key %d (%s)", idx, gAtkLatchKey,
          EntityName::Get((char*)EntryAt(idx)));
}

// ── Cast-streak AUTO latch ──────────────────────────────────────────────────
// LatchNow is the streak clock; the self-test substitutes a simulated
// time so the state machine can be exercised deterministically at the
// title screen (no world, no real casts there).
Uint64 LatchNow()
{
    return (gSelfTest && gLatchSimT != 0) ? gLatchSimT : SDL_GetTicks();
}

// One observed cast. In the real path this is fed by the SkillStage
// rising edge in LatchFrame; the self-test feeds synthetic casts
// through the same entry. skillType is the cast skill (the hero's
// CurrentSkill selection at the commit edge — every cast source
// selects before it casts). excludedSource marks a cast the standing
// laws keep out of the span: made while the physical right mouse
// button is held (RMB law), or while a gamepad skill button drives
// the cast (pad law). manualSource marks a cast the player issued by
// hand — digit cast-hold, pad skill button, or a physical mouse
// button — as opposed to AUTO's own drive casts, which carry no
// manual flag at all; that absence is what keeps AUTO's own casts
// from ever cancelling or switching AUTO (Train 12's core rule).
void LatchNoteCast(Uint64 now, int skillType, bool excludedSource, bool manualSource)
{
    // Every cast stamps the beat clock first: the anti-re-latch beat
    // keeps measuring from the player's last cast of ANY source,
    // exactly as before (decouple note: the span was never gated on
    // the keyboard master switch, MEAMU 2026-11-15).
    gLastCastT = now;
    // Self/buff/mode skills are invisible to the trigger and to the
    // OFF/SWITCH rules (Train 12): they never build or break a span,
    // never switch AUTO, never cancel it.
    if (skillType == 0 || IsSelfSkill(skillType)) return;

    if (gAutoOn) {
        gStreakStart = 0; gTrigSkill = 0;
        if (!manualSource) return;   // AUTO's own cast: hands off itself
        if (skillType == gAutoSkill) {
            // OFF rule: the player recast AUTO's skill by hand. The
            // cast already committed — nothing is swallowed; AUTO
            // simply lets go and the hand cast plays out manually.
            AutoSet(false, "manual recast of the AUTO skill", true);
        } else {
            // SWITCH rule: AUTO stays on and adopts the new skill.
            gAutoSkill = skillType;
            char name[64] = {};
            GetSkillInformation(skillType, 1, name, nullptr, nullptr, nullptr);
            if (SceneFlag == 5) Say("AUTO skill: %s", name[0] ? name : "?");
            KbLog("KB-AUTO: skill switched to type %d (%s) by manual cast",
                  skillType, name[0] ? name : "?");
        }
        return;
    }
    if (gLatchSuppress) { gStreakStart = 0; gTrigSkill = 0; return; }
    // RMB law + pad law (MEAMU 2026-11-16, G's orders): an excluded
    // cast never builds the span — it kills the span outright, so no
    // mixture of mouse/pad casting can ever latch AUTO.
    if (excludedSource) { gStreakStart = 0; gTrigSkill = 0; return; }

    if (skillType != gTrigSkill || gStreakStart == 0) {
        gTrigSkill = skillType;         // first cast — or a different
        gStreakStart = now;             // offensive skill restarts the
        gTrigLastT = now;               // span under its own name
        gTrigGapMs = kTrigGapDefMs;
        return;
    }
    const Uint64 interval = now - gTrigLastT;
    if (interval > gTrigGapMs) {        // the gap broke the span
        gStreakStart = now;
        gTrigLastT = now;
        gTrigGapMs = kTrigGapDefMs;
        return;
    }
    gTrigLastT = now;
    // The gap tolerance tracks the skill's own cycle as this hero
    // casts it: 1.5x the observed interval, clamped.
    Uint64 gap = (interval * 3) / 2;
    if (gap < kTrigGapMinMs) gap = kTrigGapMinMs;
    if (gap > kTrigGapMaxMs) gap = kTrigGapMaxMs;
    gTrigGapMs = gap;

    if (now - gStreakStart >= kLatchSpanMs) {
        const int trig = gTrigSkill;
        gStreakStart = 0; gTrigSkill = 0;
        AutoSet(true, "cast-streak latch (5 s same skill)", true);
        gAutoSkill = trig;   // AUTO engages WITH the tracked skill
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
    // as the latch's press-edge cancel in the tick samples it. Casts
    // observed while it is held are mouse casts: the RMB law excludes
    // them from the span (see LatchNoteCast).
    // Gamepad_PadCastHeld() is the pad twin of that sample: it reports
    // the gamepad module's own mirror state (any of the ten skill
    // buttons held), which the key table cannot distinguish from a
    // physical digit hold. Casts observed while it is set are pad
    // casts: the pad law excludes them the same way. A real-keyboard
    // digit hold sets neither flag and streaks exactly as before.
    // Train 12 adds the manual-source classification the OFF/SWITCH
    // rules run on: a cast is manual when a digit cast-hold is
    // driving it, a pad skill button is held, or a physical mouse
    // button is down. AUTO's own drive casts set NONE of these (its
    // buttons are synthetic and never enter the key table, its aim
    // needs no digit, and the pad is idle while it drives) — that
    // absence is the distinguisher, sampled in this same tick before
    // the drive runs.
    if (stage == 1 && gSkillStagePrev != 1) {
        const bool padHeld = Gamepad_PadCastHeld();
        const bool excluded = KeyHeld(2) || padHeld;
        const bool manual = (gCastVk != 0) || padHeld || KeyHeld(2) || KeyHeld(1);
        // The Space latch's basic attacks are not skill casts: while
        // the latch drives and no manual source is active, an edge is
        // the latch's own doing and feeds nothing.
        if (!(gAtkLatchIdx >= 0 && !manual))
            LatchNoteCast(now, SelectedSkillType(), excluded, manual);
    }
    gSkillStagePrev = stage;

    const bool world = AutoGates() || (gSelfTest && gSelfWorldSim);
    if (chat || !world || HeroDead()) { gStreakStart = 0; gTrigSkill = 0; }

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
// Tab cycle, Enter talk, the Space nearest-pick). The Space attack
// latch is decoupled from the switch as of Train 9 (a standalone
// feature, not keyboard play). AUTO is decoupled
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
        // The Space attack latch is NOT released here: it is
        // decoupled from this switch (Train 9), exactly like AUTO.
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
        LatchNoteCast(gLatchSimT, kTestSkillA, false, true);            // span opens
        gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);
        KbLog("KB-LATCH-SELFTEST: 3 casts over 800 ms: streaking=%d AUTO=%d (expect 1/0)",
              gStreakStart != 0 ? 1 : 0, gAutoOn ? 1 : 0);
        break;
    case 23: for (int k = 0; k < 11; ++k) { gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillA, false, true); }
        // span reaches 5200 ms of the SAME skill (Train 12: 5 s trigger)
        KbLog("KB-LATCH-SELFTEST: span 5200 ms reached: AUTO=%d skill=%d (expect 1/%d - latched with the tracked skill)",
              gAutoOn ? 1 : 0, gAutoSkill, kTestSkillA);
        break;
    case 24: AutoSet(false, "self-test streak exercise", false);
        LatchFrame(false);   // the off transition must engage suppression
        gLatchSimT += 300; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);   // player keeps casting
        gLatchSimT += 300; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);
        KbLog("KB-LATCH-SELFTEST: AUTO killed mid-hold: suppress=%d streaking=%d AUTO=%d (expect 1/0/0)",
              gLatchSuppress ? 1 : 0, gStreakStart != 0 ? 1 : 0, gAutoOn ? 1 : 0);
        break;
    case 25: gLatchSimT += 900;               // full cast silence: beat served
        LatchFrame(false);
        KbLog("KB-LATCH-SELFTEST: after 900 ms silence: suppress=%d (expect 0 - re-armed)",
              gLatchSuppress ? 1 : 0);
        break;
    case 26: for (int k = 0; k < 14; ++k) { gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillA, false, true); }
        KbLog("KB-LATCH-SELFTEST: fresh 5200 ms streak: AUTO=%d (expect 1 - latched again)",
              gAutoOn ? 1 : 0);
        break;
    case 27: AutoSet(false, "self-test streak exercise", false);
        LatchFrame(false);
        gLatchSimT += 900; LatchFrame(false);   // serve the beat again
        gStreakStart = 0; gTrigSkill = 0; gTrigLastT = 0; gLastCastT = 0;   // clean slate for the gap test
        gLatchSimT += 100; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);    // span opens
        gLatchSimT += 1000; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);   // cycle 1000 ms -> tolerance 1500 ms
        gLatchSimT += 1000; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);
        gLatchSimT += 2000; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);   // 2000 ms gap > tolerance: span restarts
        gLatchSimT += 1000; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);
        gLatchSimT += 1000; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);   // restarted span at 2000 ms
        KbLog("KB-LATCH-SELFTEST: gap-broken span at 2000 ms: streaking=%d AUTO=%d (expect 1/0)",
              gStreakStart != 0 ? 1 : 0, gAutoOn ? 1 : 0);
        break;
    case 28: gLatchSimT += 1000; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);
        gLatchSimT += 1000; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);
        gLatchSimT += 1000; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);   // restarted span hits 5000 ms
        KbLog("KB-LATCH-SELFTEST: restarted span reaches 5000 ms: AUTO=%d (expect 1)",
              gAutoOn ? 1 : 0);
        AutoSet(false, "self-test streak exercise", false);
        KbLog("KB-LATCH-SELFTEST: segment complete (AUTO on=%d, expect 0) - AUTOSWITCH segment follows", gAutoOn ? 1 : 0);
        break;
    case 29: KbLog("KB-AUTOSWITCH-SELFTEST: segment starts - OFF/SWITCH rules under a forced AUTO (Train 12)");
        gSelfForced = true;
        gSelfWorldSim = true;
        AutoSet(true, "self-test autoswitch exercise", false);
        gAutoSkill = kTestSkillA;   // title screen has no selection to snapshot; stamp the test skill directly
        LatchNoteCast(gLatchSimT, kTestSkillA, false, false);            // AUTO's own cast
        gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillA, false, false);
        KbLog("KB-AUTOSWITCH-SELFTEST: AUTO's own casts: AUTO=%d skill=%d (expect 1/%d - never self-cancels)",
              gAutoOn ? 1 : 0, gAutoSkill, kTestSkillA);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT, 59, false, true);   // manual self skill (Sage Mode)
        KbLog("KB-AUTOSWITCH-SELFTEST: manual self-skill cast: AUTO=%d skill=%d (expect 1/%d - invisible)",
              gAutoOn ? 1 : 0, gAutoSkill, kTestSkillA);
        break;
    case 30: gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillB, false, true);   // manual cast of a different offensive skill
        KbLog("KB-AUTOSWITCH-SELFTEST: manual cast of skill B: AUTO=%d skill=%d (expect 1/%d - switched)",
              gAutoOn ? 1 : 0, gAutoSkill, kTestSkillB);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillB, false, false);       // AUTO's own cast of the new skill
        KbLog("KB-AUTOSWITCH-SELFTEST: AUTO's own cast of B: AUTO=%d skill=%d (expect 1/%d)",
              gAutoOn ? 1 : 0, gAutoSkill, kTestSkillB);
        break;
    case 31: gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillB, false, true);   // manual recast of AUTO's skill
        KbLog("KB-AUTOSWITCH-SELFTEST: manual recast of B: AUTO=%d (expect 0 - OFF rule)",
              gAutoOn ? 1 : 0);
        break;
    case 32: LatchFrame(false);   // serve the anti-re-latch beat opened by the OFF
        gLatchSimT += 900; LatchFrame(false);
        gStreakStart = 0; gTrigSkill = 0; gTrigLastT = 0;   // clean slate
        LatchNoteCast(gLatchSimT, kTestSkillA, false, true);            // span opens on A
        gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillB, false, true);   // B restarts the span
        gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillB, false, true);
        KbLog("KB-AUTOSWITCH-SELFTEST: A-span abandoned for B: trigSkill=%d streaking=%d AUTO=%d (expect %d/1/0)",
              gTrigSkill, gStreakStart != 0 ? 1 : 0, gAutoOn ? 1 : 0, kTestSkillB);
        break;
    case 33: gLatchSimT = 0;
        gSelfWorldSim = false;
        gSelfForced = false;
        KbLog("KB-AUTOSWITCH-SELFTEST: segment complete (AUTO on=%d, expect 0)", gAutoOn ? 1 : 0);
        break;
    case 34: KbLog("KB-CLOBBER-SELFTEST: segment starts - physical RMB hold simulated (RMB-law hold: its casts must never latch)");
        gSelfForced = true;
        gSelfWorldSim = true;
        gLatchSimT = 700000;
        gLatchSuppress = false; gStreakStart = 0; gTrigSkill = 0; gTrigLastT = 0; gLastCastT = 0;   // clean slate
        Platform::InjectVirtualKey(2, true);    // physical RMB goes down
        DAT_083a42ac = 1;                        // WndProc state from that real press
        KbLog("KB-CLOBBER-SELFTEST: RMB held=%d shared-flag=%d (expect 1/1)",
              KeyHeld(2) ? 1 : 0, (int)DAT_083a42ac);
        break;
    case 35: for (int k = 0; k < 6; ++k) { LatchNoteCast(gLatchSimT, kTestSkillA, KeyHeld(2), KeyHeld(2)); gLatchSimT += 400; }
        // casts at T..T+2000 under the held RMB, fed through the same
        // source-aware entry the real tick uses: the RMB law (2026-11-16)
        // says the span can never complete — AUTO must stay off.
        KbLog("KB-CLOBBER-SELFTEST: 2 s of casts under a held RMB: AUTO=%d RMB held=%d (expect 0/1 - no latch from mouse casting)",
              gAutoOn ? 1 : 0, KeyHeld(2) ? 1 : 0);
        AutoSet(true, "self-test clobber exercise", false);   // arm AUTO directly (the F8 path) — the clobber scenario below needs a running drive, not a latch
        AutoDriveButton();   // the drive's first button step
        KbLog("KB-CLOBBER-SELFTEST: drive button step: synth-held=%d shared-flag=%d (expect 1/1)",
              gRightDown ? 1 : 0, (int)DAT_083a42ac);
        break;
    case 36: Platform::InjectVirtualKey(2, false);   // player lets go of the mouse
        DAT_083a42ac = 0;   // the real RightUp clears the shared flag (Input_WndProc)
        KbLog("KB-CLOBBER-SELFTEST: RMB released: held=%d AUTO=%d (expect 0/1 - the pre-existing hold's release is not an off input)",
              KeyHeld(2) ? 1 : 0, gAutoOn ? 1 : 0);
        AutoDriveButton();   // the drive must re-assert the clobbered hold
        KbLog("KB-CLOBBER-SELFTEST: after drive step: shared-flag=%d synth-held=%d (expect 1/1 - hold re-asserted, AUTO keeps casting)",
              (int)DAT_083a42ac, gRightDown ? 1 : 0);
        break;
    case 37: KbLog("KB-CLOBBER-SELFTEST: injecting a NEW physical RMB press - AUTO must STAY ON (Train 12 cursor freedom: a press yields the drive for its moment, it no longer cancels)");
        Platform::InjectVirtualKey(2, true);
        DAT_083a42ac = 1;   // WndProc state from the real press (already 1 via the synth hold)
        break;
    case 38: KbLog("KB-CLOBBER-SELFTEST: after new press: AUTO=%d (expect 1 - no mouse-takeover cancel since Train 12)",
              gAutoOn ? 1 : 0);
        Platform::InjectVirtualKey(2, false);
        AutoSet(false, "self-test clobber exercise", false);   // the test takes AUTO down itself
        gLatchSimT = 0;
        gSelfWorldSim = false;
        gSelfForced = false;
        KbLog("KB-CLOBBER-SELFTEST: segment complete (AUTO on=%d, expect 0)", gAutoOn ? 1 : 0);
        break;
    case 39: KbLog("KB-DECOUPLE-SELFTEST: segment starts - AUTO with keyboard controls OFF (KB on=%d, AUTO on=%d, expect AUTO 0)",
               gKbEnabled ? 1 : 0, gAutoOn ? 1 : 0);
        gSelfForced = true;
        gSelfWorldSim = true;
        if (gKbEnabled) KbSet(false, "self-test decouple exercise");
        KbLog("KB-DECOUPLE-SELFTEST: KB on=%d (expect 0). Injecting F8 down - AUTO must engage with KB off",
              gKbEnabled ? 1 : 0);
        Platform::InjectVirtualKey(kAutoVk, true);
        break;
    case 40: Platform::InjectVirtualKey(kAutoVk, false);
        KbLog("KB-DECOUPLE-SELFTEST: after F8: AUTO on=%d (expect 1 - engaged with KB off)",
              gAutoOn ? 1 : 0);
        AutoSet(false, "self-test decouple exercise", false);
        LatchFrame(false);   // the off transition engages the anti-re-latch beat...
        gLatchSimT = 900000;
        gLatchSimT += 900; LatchFrame(false);   // ...which a 900 ms silence then serves
        gLatchSuppress = false; gStreakStart = 0; gTrigSkill = 0; gTrigLastT = 0; gLastCastT = 0;   // clean slate regardless
        LatchNoteCast(gLatchSimT, kTestSkillA, false, true);            // span opens with KB off
        gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);
        gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillA, false, true);
        KbLog("KB-DECOUPLE-SELFTEST: 3 casts over 800 ms with KB off: streaking=%d AUTO=%d (expect 1/0)",
              gStreakStart != 0 ? 1 : 0, gAutoOn ? 1 : 0);
        break;
    case 41: for (int k = 0; k < 11; ++k) { gLatchSimT += 400; LatchNoteCast(gLatchSimT, kTestSkillA, false, true); }
        // span reaches 5200 ms with KB off (Train 12: 5 s trigger)
        KbLog("KB-DECOUPLE-SELFTEST: span 5200 ms with KB off: AUTO=%d (expect 1 - streak latched with KB off)",
              gAutoOn ? 1 : 0);
        KbSet(true, "self-test decouple exercise");    // flip ON mid-AUTO
        KbLog("KB-DECOUPLE-SELFTEST: F9 flip ON mid-AUTO: KB on=%d AUTO on=%d (expect 1/1)",
              gKbEnabled ? 1 : 0, gAutoOn ? 1 : 0);
        KbSet(false, "self-test decouple exercise");   // flip OFF mid-AUTO
        KbLog("KB-DECOUPLE-SELFTEST: F9 flip OFF mid-AUTO: KB on=%d AUTO on=%d (expect 0/1)",
              gKbEnabled ? 1 : 0, gAutoOn ? 1 : 0);
        break;
    case 42: AutoSet(false, "self-test decouple exercise", false);
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

// MEAMU Train 31: ActionMove's scheme flip calls this when entering
// Action — a latch engaged in Classic never carries its walk-in over.
void KbPlay_ClearAttackLatch() { AtkLatchRelease("scheme flip to Action", false); }

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

// Space hook for HUD_HotkeyTick (Game/Player_InputTick.cpp), the Tab
// pattern: true = the tap was consumed by the attack latch, false = Space
// keeps its stock job (pick up the nearest ground item). Space's new job
// wins whenever there is something to attack — the latch engages on the
// closest attackable monster on screen, or cancels a running latch —
// and the pickup keeps the key only when no attackable target exists
// on screen, or the world gates are closed (chat included:
// HUD_HotkeyTick never reaches here while typing).
//
// MEAMU Train 9 (2026-11-19): the latch is STANDALONE — decoupled from
// the keyboard master switch, the same treatment AUTO got 2026-11-15.
// Train 8 gated the engage on KbEnabled(), so any player with keyboard
// controls OFF (a mouse-first player; the switch persists in
// Config.ini) got a silently dead Space: every tap declined to the
// pickup fallback and nothing happened near mobs (G's field report on
// v20261119-0203: "it doesnt work"). Gates() alone is the right
// bound — its InputEnable term keeps Space out of the chat line — and
// the drive in the tick already ran switch-free on AutoGates().
bool KbPlay_SpaceTap()
{
    // MEAMU Train 13: in the ACTION control scheme the Space latch does
    // not engage — mouse aim owns targeting there — so Space keeps its
    // stock pickup job. CLASSIC is untouched.
    if (ActionMove_IsAction()) return false;
    if (!Gates()) return false;

    if (gAtkLatchIdx >= 0) {
        AtkLatchRelease("space tap", true);
        return true;
    }
    const int t = AcquireNearestBasic();
    if (t < 0) return false;
    AtkLatchEngage(t);
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

    // Cursor tracking (Train 12): a game-cursor position change since
    // last tick means the player is moving the mouse now; the stamp
    // feeds the stillness test the drives use before they may park.
    // Our own parks never trip this — they land synchronously and the
    // end-of-tick refresh below records the parked point.
    if ((int)DAT_083a427c != gMouseUiX || (int)DAT_083a4278 != gMouseUiY)
        gCursorChangeT = SDL_GetTicks();

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
    if (AutoGates() && Attacking == 1 && ValidLatchTarget(DAT_00559c70))
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

    // Physical mouse buttons. The key table's button bits are written
    // only by real SDL device events (pad key pulses never use buttons
    // 1/2); virtual buttons from this module, NavMove and the gamepad
    // travel via DeliverSDLInput and never appear here, so this cannot
    // misfire on synthesized input.
    // Train 12 (cursor freedom): a physical press NO LONGER cancels
    // AUTO — the mouse belongs to the player, and the AUTO drive
    // yields to it per-tick instead (AutoDriveStep: no park and no
    // synthetic hold while a physical button is down). The Space
    // attack latch keeps its own takeover rule: a fresh physical
    // press is the player taking the latch back.
    const bool mL = KeyHeld(1), mR = KeyHeld(2);
    if (gAtkLatchIdx >= 0 && ((mL && !gMouseLPrev) || (mR && !gMouseRPrev)))
        AtkLatchRelease("manual mouse input", true);
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
            if (SelectedSkillType() != 0) { AutoDriveStep(); }
            else SetRightButton(false);   // empty slot selected: stand + scan
        }
    }

    // ── Space attack latch drive ─────────────────────────────────────────────
    // AUTO and the latch never drive at once (one cursor): engaging AUTO
    // (F8 or the cast streak) drops the latch; engaging the latch drops
    // AUTO (AtkLatchEngage). The drive runs on AutoGates() like AUTO's:
    // it keeps attacking while the chat input is open — Space belongs to
    // the chat there, so a cancel tap only lands with the chat closed.
    if (gAutoOn && gAtkLatchIdx >= 0)
        AtkLatchRelease("AUTO takes the drive", false);
    if (gAtkLatchIdx >= 0) {
        if (!AutoGates()) {
            AtkLatchRelease(SceneFlag != 5 ? "scene change" : "gate closed (modal)", false);
        } else if (HeroDead()) {
            AtkLatchRelease("character death", true);
        } else if (!ValidLatchTarget(gAtkLatchIdx) ||
                   (int)EntryAt(gAtkLatchIdx)->Key != gAtkLatchKey) {
            // The spec's core rule: the latch releases the moment the
            // target dies — ValidLatchTarget also fails on despawn, and the
            // Key guard catches a pool slot recycled to a different body.
            // The hero stops; the next Space tap locks the next target.
            AtkLatchRelease("target gone", false);
        } else if (gCastVk || Gamepad_IsDriving()) {
            // A hand-held digit cast or the gamepad outranks the latch
            // while it drives (AUTO's yield rule); the latch resumes after.
            if (!gAtkYielded) {
                gAtkYielded = true;
                SetLeftButton(false);
                KbLog("KB-ATK: latch yields the button");
            }
        } else {
            if (gAtkYielded) { gAtkYielded = false; KbLog("KB-ATK: latch re-engages"); }
            // MEAMU Train 20: the latch can be live under the pointer
            // lock (engaged in CLASSIC, then a scheme flip to ACTION).
            // While locked, close on the locked target by direct route;
            // the park discipline below reads the pinned centre cursor
            // and cannot steer.
            LockedWalkInStep(gAtkLatchIdx, 1);
            // Cursor freedom (Train 12): the latch's target is locked,
            // so the old per-tick park was pure seizure. Park only
            // when the target's screen position drifts materially and
            // the player is not moving the mouse; hold the virtual
            // button only while the cursor sits on the target.
            CHARACTER* tgt = EntryAt(gAtkLatchIdx);
            int ux = 0, uy = 0;
            ProjectUi(tgt, ux, uy);
            if (UiDist(ux, uy, gParkUiX, gParkUiY) > kParkDriftPx && !PlayerMovingMouse()) {
                ParkOnCharacter(tgt);
                AtkDriveButton();
            } else if (UiDist((int)DAT_083a427c, (int)DAT_083a4278, ux, uy) <= kCursorAimPx) {
                AtkDriveButton();
            } else if (gAtkLeftDown) {
                SetLeftButton(false);
            }
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

    // End-of-tick cursor record (Train 12): where the game cursor
    // ended this tick, after every drive — the reference the next
    // tick's player-movement and stillness tests compare against.
    gMouseUiX = (int)DAT_083a427c;
    gMouseUiY = (int)DAT_083a4278;
}

// HUD indicator (RenderInformation, next to the gamepad's "PAD"): lit for
// the whole AUTO run, chat sessions included — it reads only the state.
void KbPlay_RenderAutoIndicator()
{
    if (!gAutoOn) return;
    UI_RenderText(532, 6, (char*)"AUTO", (LPSIZE)0x0, '\0', 0);
}
