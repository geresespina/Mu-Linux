// MoveMarkers.cpp — see MoveMarkers.h for the design. Mirrors the
// fork's module shapes: BloodGore (receive/input-fed state + HUD-pass
// render with Camera_ProjectWorldToScreen + BufferedDraw), GL_DrawRect
// (untextured colour geometry: GL_SetAlphaTest('\0') disables texture
// sampling, then plain BufferedDraw shapes in Gfx::Color4f colour).
// No new art assets, no bitmap slots, no particle types.

#include "stdafx.h"
#include "Net/MoveMarkers.h"
#include "Config/UserSettings.h"
#include "Core/Window.h"
#include "Platform/ITimer.h"
#include "Render/BufferedDraw.h"
#include "Render/Gfx.h"
#include "globals.h"
#include "functions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

using namespace MoveMarkers;

constexpr uint32_t kClickLifeMs = 550;    // spec: ~0.5-0.6 s
constexpr int      kRingSegs    = 28;
constexpr float    kCullDist2   = 2600.0f * 2600.0f;  // BloodGore's cull

struct ClickState {
    bool     active = false;
    float    x = 0, y = 0, z = 0;
    uint32_t born = 0;
};
ClickState s_Click;

// Mouse-only gate state: the Platform::RealInputSerial value seen at
// the previous evaluated ground-click pulse. Synthesized input (pad,
// KbPlay, NavMove) is delivered via Platform::DeliverSDLInput and
// never passes the SDL event watch, so it never advances the serial;
// a pulse whose serial did not advance is not a physical mouse press.
unsigned s_LastPulseSerial = 0;
bool     s_HavePulseSerial = false;

uint32_t NowMs()
{
    return (uint32_t)Platform::GetTimer().Ticks();
}

bool Enabled()
{
    return gUserSettings.GetMoveMarker();
}

float EaseOutCubic(float t)
{
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

void SpawnClick(float wx, float wy, float wz, uint32_t now)
{
    s_Click.active = true;
    s_Click.x = wx; s_Click.y = wy; s_Click.z = wz;
    s_Click.born = now;
}

// Project a world point to BufferedDraw GL coords (the BloodGore
// conversion). False when the point lands wildly off-screen.
bool Project(float wx, float wy, float wz, float* gx, float* gy)
{
    float wp[3] = { wx, wy, wz };
    int sx = 0, sy = 0;
    Camera_ProjectWorldToScreen(wp, &sx, &sy);
    if (sx < -640 || sx > 1280 || sy < -480 || sy > 960) return false;
    *gx = (float)Screen_ToGLX((float)sx);
    *gy = (float)gWindow.GetHeight() - (float)Screen_ToGLY((float)sy);
    return true;
}

// Flat annulus on the ground plane (z + 2, like the BloodGore splats),
// drawn as a projected triangle strip. Skipped whole if any vertex
// projects off-screen (BloodGore's guard).
void DrawGroundRing(float cx, float cy, float cz, float rOut, float rIn,
                    float r, float g, float b, float alpha)
{
    float gx[(kRingSegs + 1) * 2], gy[(kRingSegs + 1) * 2];
    for (int i = 0; i <= kRingSegs; ++i) {
        const float a = (float)i / (float)kRingSegs * 6.2831853f;
        const float ca = std::cos(a), sa = std::sin(a);
        if (!Project(cx + ca * rOut, cy + sa * rOut, cz + 2.0f,
                     &gx[i * 2], &gy[i * 2])) return;
        if (!Project(cx + ca * rIn, cy + sa * rIn, cz + 2.0f,
                     &gx[i * 2 + 1], &gy[i * 2 + 1])) return;
    }
    Gfx::Color4f(r, g, b, alpha);
    BufferedDraw::Begin(BufferedDraw::Primitive::TriangleStrip);
    for (int i = 0; i <= kRingSegs * 2 + 1; ++i)
        BufferedDraw::Vertex2f(gx[i], gy[i]);
    BufferedDraw::End();
}


// Downward chevron (filled triangle) hovering over the projected
// centre, descending toward the tile as the marker ages.
void DrawChevron(float cx, float cy, float cz, float t,
                 float r, float g, float b, float alpha)
{
    float gx = 0, gy = 0;
    if (!Project(cx, cy, cz + 2.0f, &gx, &gy)) return;
    const float drop = 34.0f - 16.0f * t;   // GL px above the point
    Gfx::Color4f(r, g, b, alpha);
    BufferedDraw::Begin(BufferedDraw::Primitive::Triangles);
    BufferedDraw::Vertex2f(gx - 9.0f, gy + drop);
    BufferedDraw::Vertex2f(gx + 9.0f, gy + drop);
    BufferedDraw::Vertex2f(gx, gy + drop - 11.0f);
    BufferedDraw::End();
}

bool HeroPos(float* hx, float* hy)
{
    const unsigned char* hero = (const unsigned char*)(uintptr_t)DAT_07abf5d8;
    if (!hero || !hero[0]) return false;
    *hx = *(const float*)(hero + 0x10);
    *hy = *(const float*)(hero + 0x14);
    return true;
}

bool NearHero(float x, float y)
{
    float hx = 0, hy = 0;
    if (!HeroPos(&hx, &hy)) return true;
    const float dx = x - hx, dy = y - hy;
    return dx * dx + dy * dy <= kCullDist2;
}

void RunSelfTestOnce()
{
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    if (!std::getenv("MU_MOVEMARKERS_SELFTEST")) return;

    float hx = 0, hy = 0;
    if (!HeroPos(&hx, &hy)) {
        std::printf("MOVEMARKERS SELFTEST: SKIP (no hero)\n");
        std::fflush(stdout);
        return;
    }
    const uint32_t now = NowMs();
    SpawnClick(hx + 200.0f, hy, 100.0f, now);
    const bool ok = ActiveClickMarker() == 1;
    std::printf("MOVEMARKERS SELFTEST: %s (click %d)\n",
                ok ? "PASS" : "FAIL", ActiveClickMarker());
    std::fflush(stdout);
    ClearAll();
}

} // namespace

namespace MoveMarkers {

void OnGroundClick(int gridX, int gridY, bool clickEdge,
                   unsigned realSerial, bool virtualDriving,
                   bool physicalDown)
{
    if (!Enabled()) return;
    if (!clickEdge) return;                    // hold re-paths: no edge
    const bool serialAdvanced =
        !s_HavePulseSerial || realSerial != s_LastPulseSerial;
    s_LastPulseSerial = realSerial;            // consume the pulse either way
    s_HavePulseSerial = true;
    if (!serialAdvanced) return;               // synthesized press
    if (virtualDriving) return;                // pad / follow / arrows steering
    if (!physicalDown) return;                 // SDL watch: real LMB not held

    const float wx = ((float)gridX + 0.5f) * 100.0f;
    const float wy = ((float)gridY + 0.5f) * 100.0f;
    SpawnClick(wx, wy, RequestTerrainHeight(wx, wy), NowMs());
}



int ActiveClickMarker()
{
    if (!s_Click.active) return 0;
    return (NowMs() - s_Click.born <= kClickLifeMs) ? 1 : 0;
}





void ClearAll()
{
    s_Click.active = false;
}

} // namespace MoveMarkers

void Render_MoveMarkers(void)
{
    RunSelfTestOnce();
    if (!Enabled()) return;
    if (SceneFlag != 5) return;
    if (!CharactersClient) return;
    const uint32_t now = NowMs();

    bool drew = false;
    const auto beginDraw = [&]() {
        if (drew) return;
        drew = true;
        EnableAlphaTest(true);
        EnableAlphaBlend();
        GL_SetAlphaTest('\0');   // DisableTexture: untextured geometry
    };

    if (s_Click.active) {
        const uint32_t age = now - s_Click.born;
        if (age <= kClickLifeMs && NearHero(s_Click.x, s_Click.y)) {
            const float t = (float)age / (float)kClickLifeMs;
            const float fade = 1.0f - t;
            beginDraw();
            const float rOut = 10.0f + (48.0f - 10.0f) * EaseOutCubic(t);
            DrawGroundRing(s_Click.x, s_Click.y, s_Click.z, rOut, rOut - 7.0f,
                           1.0f, 0.85f, 0.45f, 0.95f * fade);
            DrawGroundRing(s_Click.x, s_Click.y, s_Click.z, 11.0f, 8.5f,
                           1.0f, 0.85f, 0.45f, 0.8f * fade);
            DrawChevron(s_Click.x, s_Click.y, s_Click.z, t,
                        1.0f, 0.85f, 0.45f, 0.95f * fade);
        }
    }

    if (drew) {
        Gfx::Color4f(1.0f, 1.0f, 1.0f, 1.0f);
        GL_ResetState();
    }
}
