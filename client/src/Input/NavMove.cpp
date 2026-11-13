// NavMove.cpp — MEAMU rev.5: arrow-key movement + client-side /follow.
//
// Both features steer the hero through the same mechanism as the gamepad's
// left stick: park the virtual cursor ahead of the hero and hold the virtual
// left button — the game's native hold-to-move continuously re-paths. Input
// is synthesized through Platform::DeliverSDLInput, the real input path.
//
// Arrow keys: hold = keep walking in that direction (two keys = diagonal),
// release = stop. A ground click retakes control naturally (a real click
// issues its own path; the arrows simply stop being the driver once
// released). Gates: in-world scene, chat input closed, no system menu open.
// While arrows are held, the gamepad's left stick yields movement ownership.
//
// /follow <name>: the client re-paths toward the named player's position
// every 400 ms and stops within 2 tiles. It ends on: /follow with no name,
// any real device input (mouse click or key press — Platform::RealInputSerial
// advances only on genuine SDL input events), arrow keys, the target leaving
// the client's character table for >3 s (logout / map change), or leaving
// the world. While the gamepad is actively driving, follow pauses.

#include "stdafx.h"
#include "Input/NavMove.h"
#include "Input/Gamepad.h"       // Gamepad_IsDriving (mutual yield)
#include "Platform/IInput.h"     // Platform::InputEvent, GetInput
#include "Platform/Text.h"       // Platform::TextCompareIgnoreCase(N)
#include "Core/Window.h"         // gWindow
#include "Entity/EntityTypes.h"  // CHARACTER, CHARACTER_STRIDE
#include "Entity/EntityName.h"   // EntityName::Get
#include <SDL3/SDL.h>            // SDL_GetTicks
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>

extern "C" void DbgLogPublic(const char* msg);

namespace Platform {
void DeliverSDLInput(const InputEvent& event);   // SDLWindow.cpp
unsigned RealInputSerial();                      // SDLInput.cpp (rev.5 export)
}

namespace {

constexpr float kNavRadius      = 0.22f;   // of min(winW, winH), as the pad
constexpr int   kFollowArrive   = 2;       // tiles (Chebyshev)
constexpr unsigned kFollowPathMs = 400;    // re-path interval
constexpr unsigned kFollowLostMs = 3000;   // target missing -> stop

bool  gNavHeld = false;                    // this module's virtual LMB state
float gNavX = 0.0f, gNavY = 0.0f;          // last parked cursor (window px)
bool  gArrowsHeldRaw = false;              // any arrow key down this frame

bool     gFollowActive = false;
char     gFollowName[32] = {};
unsigned gFollowSerial = 0;                // RealInputSerial at (re)start
unsigned gFollowLastPath = 0;
unsigned gFollowMissingSince = 0;

void NavLog(const char* fmt, ...)
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
    NavLog("NAV: %s", buf);
}

void SendMouse(Platform::InputEventType type, float x, float y)
{
    Platform::InputEvent ev{};
    ev.type = type;
    ev.x = (int)lroundf(x);
    ev.y = (int)lroundf(y);
    Platform::DeliverSDLInput(ev);
}

void SetNavButton(bool down)
{
    if (down == gNavHeld) return;
    gNavHeld = down;
    SendMouse(down ? Platform::InputEventType::LeftDown
                   : Platform::InputEventType::LeftUp, gNavX, gNavY);
}

void ParkCursor(float x, float y)
{
    gNavX = x; gNavY = y;
    SendMouse(Platform::InputEventType::MouseMove, x, y);
}

bool KeyHeld(int vk)
{
    return ((unsigned short)Platform::GetInput().KeyState(vk) & 0x8000) != 0;
}

// Same validity predicate the chat interaction commands use
// (Chat_FindPlayerInteractionTarget): live player entry in the client table.
char* FindPlayerByName(const char* name)
{
    if (!name || !*name || !CharactersClient) return nullptr;
    char* base = (char*)(uintptr_t)CharactersClient;
    for (int i = 0; i < 400; ++i) {
        char* c = base + i * CHARACTER_STRIDE;
        if (c[0] && *(short*)(c + 2) == 390 && c[132] == 1 &&
            Platform::TextCompareIgnoreCase(EntityName::Get(c), name) == 0)
            return c;
    }
    return nullptr;
}

void StopFollow()
{
    gFollowActive = false;
    gFollowName[0] = '\0';
    SetNavButton(false);
}

void FollowDrive(unsigned now, float winW, float winH)
{
    if (now - gFollowLastPath < kFollowPathMs) return;   // keep current path
    gFollowLastPath = now;

    char* target = FindPlayerByName(gFollowName);
    if (!target) {
        if (!gFollowMissingSince) gFollowMissingSince = now;
        if (now - gFollowMissingSince > kFollowLostMs) {
            char name[32];
            std::strncpy(name, gFollowName, sizeof(name) - 1); name[sizeof(name) - 1] = '\0';
            StopFollow();
            Say("Follow: lost %s - stopped.", name);
        } else {
            SetNavButton(false);
        }
        return;
    }
    gFollowMissingSince = 0;

    CHARACTER* hero = (CHARACTER*)(uintptr_t)Hero;
    CHARACTER* tgt  = (CHARACTER*)(uintptr_t)target;
    const int dx = tgt->PositionX - hero->PositionX;
    const int dy = tgt->PositionY - hero->PositionY;
    const int cheb = (std::abs(dx) > std::abs(dy)) ? std::abs(dx) : std::abs(dy);
    if (cheb <= kFollowArrive) { SetNavButton(false); return; }

    // Direction: project hero + target to screen and steer along the delta —
    // robust to the camera angle without assuming an isometric mapping.
    int hsx = 0, hsy = 0, tsx = 0, tsy = 0;
    Camera_ProjectWorldToScreen((float*)hero->Object.Position, &hsx, &hsy);
    Camera_ProjectWorldToScreen((float*)tgt->Object.Position, &tsx, &tsy);
    float dirx = (tsx - hsx) * winW / 640.0f;
    float diry = (tsy - hsy) * winH / 480.0f;
    float len = sqrtf(dirx * dirx + diry * diry);
    if (len < 1.0f) {   // projection degenerate: isometric fallback from tiles
        dirx = (float)(dx - dy);
        diry = (float)(dx + dy) * 0.5f;
        len = sqrtf(dirx * dirx + diry * diry);
        if (len < 0.001f) { SetNavButton(false); return; }
    }
    dirx /= len; diry /= len;
    const float radius = kNavRadius * (winW < winH ? winW : winH);
    ParkCursor(winW * 0.5f + dirx * radius, winH * 0.5f + diry * radius);
    SetNavButton(true);
}

} // namespace

bool NavMove_ArrowsHeld() { return gArrowsHeldRaw; }
bool Follow_IsActive() { return gFollowActive; }

bool Follow_TryCommand(const char* text)
{
    if (!text) return false;
    if (Platform::TextCompareIgnoreCaseN(text, "/follow", 7) != 0) return false;
    if (text[7] != '\0' && text[7] != ' ') return false;

    const char* arg = text + 7;
    while (*arg == ' ') ++arg;
    if (*arg == '\0' || Platform::TextCompareIgnoreCase(arg, "off") == 0 ||
        Platform::TextCompareIgnoreCase(arg, "stop") == 0) {
        if (gFollowActive) { StopFollow(); Say("Follow: stopped."); }
        return true;
    }

    char name[32];
    std::strncpy(name, arg, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    for (int i = (int)std::strlen(name) - 1; i >= 0 && name[i] == ' '; --i) name[i] = '\0';

    if (!FindPlayerByName(name)) {
        Say("Follow: player '%s' not found.", name);
        return true;
    }
    std::strncpy(gFollowName, name, sizeof(gFollowName) - 1);
    gFollowName[sizeof(gFollowName) - 1] = '\0';
    gFollowActive = true;
    gFollowSerial = Platform::RealInputSerial();   // the Enter key is already counted
    gFollowLastPath = 0;
    gFollowMissingSince = 0;
    Say("Follow: following %s - /follow stops.", gFollowName);
    return true;
}

void NavMove_Tick()
{
    const bool inWorld = (SceneFlag == 5) && Hero != nullptr;
    gArrowsHeldRaw = KeyHeld(37) || KeyHeld(38) || KeyHeld(39) || KeyHeld(40);

    const float winW = (float)gWindow.GetRealWidth();
    const float winH = (float)gWindow.GetRealHeight();
    const unsigned now = (unsigned)SDL_GetTicks();

    // ── Follow ───────────────────────────────────────────────────────────────
    if (gFollowActive) {
        if (!inWorld) {
            StopFollow();
        } else if (Platform::RealInputSerial() != gFollowSerial || gArrowsHeldRaw) {
            StopFollow();   // any real input takes control back
            NavLog("NAV: follow cancelled by real input");
        } else if (Gamepad_IsDriving()) {
            SetNavButton(false);   // pad is driving: pause, stay engaged
        } else if (InputEnable == 0 && DAT_083a7c24 == 0) {
            FollowDrive(now, winW, winH);
        } else {
            SetNavButton(false);
        }
    }

    // ── Arrow keys ───────────────────────────────────────────────────────────
    // In-world only, chat input closed, system menu closed. While the pad is
    // steering, arrows yield (Gamepad checks NavMove_ArrowsHeld and yields
    // its own stick in turn — the two never fight over the cursor).
    const bool gates = inWorld && InputEnable == 0 && DAT_083a7c24 == 0;
    if (gates && gArrowsHeldRaw && !Gamepad_IsDriving()) {
        const float dx = (KeyHeld(39) ? 1.0f : 0.0f) - (KeyHeld(37) ? 1.0f : 0.0f);
        const float dy = (KeyHeld(40) ? 1.0f : 0.0f) - (KeyHeld(38) ? 1.0f : 0.0f);
        const float len = sqrtf(dx * dx + dy * dy);
        if (len > 0.0f) {
            const float radius = kNavRadius * (winW < winH ? winW : winH);
            ParkCursor(winW * 0.5f + dx / len * radius, winH * 0.5f + dy / len * radius);
            SetNavButton(true);
            return;
        }
    }
    if (!gFollowActive) SetNavButton(false);
}
