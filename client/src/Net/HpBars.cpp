// HpBars.cpp — see HpBars.h for the design. Mirrors the fork's
// existing module shapes: MapTrack (F3 custom receive + table) for
// the wire half, CHealthBar::DrawViewport (projection + distance
// scale + GL_DrawRect incantation) for the render half.

#include "stdafx.h"
#include "Net/HpBars.h"
#include "Entity/EntityView.h"
#include "Entity/EntityTypes.h"
#include "Game/HeroVitals.h"
#include "Render/Gfx.h"
#include "Platform/ITimer.h"
#include "globals.h"
#include "functions.h"

#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" SIZE* __cdecl RenderCenteredText(int x, int y, const char* text);

namespace {

struct HpRecord {
    uint16_t perMille = 0;
    uint32_t lastTick = 0;
    bool active = false;
};

// Server object indices are 15-bit on the wire (the combat handlers
// mask with 0x7FFF); one flat table, O(1) both ways.
HpRecord s_Records[0x8000];

uint32_t NowMs()
{
    return (uint32_t)Platform::GetTimer().Ticks();
}

bool RecordFresh(const HpRecord& rec, uint32_t now)
{
    return rec.active && (now - rec.lastTick) <= HpBars::HPBAR_FADE_MS;
}

void Stamp(int index, uint16_t perMille, uint32_t now)
{
    HpRecord& rec = s_Records[index & 0x7FFF];
    rec.perMille = perMille > 1000 ? 1000 : perMille;
    rec.lastTick = now;
    rec.active = true;
}

// Boss classes that also print their numeric % on the bar — the
// %-fight layer (damage talk is % in this house). 155-164 = the ten
// wild Biju; 174/175 = Selupan / Winged Balgass (the same world
// bosses the stock bar's carve-out already recognises); 193-197 =
// the Multiverse boss rotation (Kaido, Porunga, Majin Buu, Zamasu,
// Exodia).
bool IsBossClass(int cls)
{
    return (cls >= 155 && cls <= 164) || cls == 174 || cls == 175 ||
           (cls >= 193 && cls <= 197);
}

// The hero's own bar needs no wire: gHeroVitals already carries the
// exact 32-bit Life/MaxLife the server pushes (0x15/0x26 View*
// fields). A drop versus last frame = the hero took damage; stamp
// his record with the same fraction semantics as an F3/F7 record.
// Heals/regen raise the value and never light the bar. When the GS
// half is live, the F3/F7 self-copy writes the same record with the
// same fraction — the two writers agree by construction.
void TrackHero(uint32_t now)
{
    static const BYTE* s_HeroPtr = nullptr;
    static DWORD s_PrevLife = 0;

    const BYTE* hero = (const BYTE*)(uintptr_t)DAT_07abf5d8;
    if (!hero) { s_HeroPtr = nullptr; return; }

    const DWORD cur = gHeroVitals.GetCurrent(VITAL_LIFE);
    const DWORD max = gHeroVitals.GetMax(VITAL_LIFE);

    if (hero != s_HeroPtr) { s_HeroPtr = hero; s_PrevLife = cur; return; }

    if (max > 0 && cur < s_PrevLife) {
        const EntityView view(hero);
        const uint16_t perMille = (uint16_t)((uint64_t)cur * 1000 / max);
        Stamp(view.NetworkKey(), perMille, now);
    }
    s_PrevLife = cur;
}

void DrawPercentText(int centerX, int y, uint16_t perMille)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%u.%u%%",
                  (unsigned)(perMille / 10), (unsigned)(perMille % 10));
    const DWORD savedBack = m_dwBackColor;
    const DWORD savedText = m_dwTextColor;
    HGDIOBJ oldFont = gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_NORMAL));
    EnableAlphaTest(true);
    Gfx::Color4f(1, 1, 1, 1);
    m_dwBackColor = 0;
    m_dwTextColor = 0xFFFFFFFFu;
    RenderCenteredText(centerX, y, text);
    m_dwBackColor = savedBack;
    m_dwTextColor = savedText;
    gFont.SelectFont(gFont.GetTextDC(), oldFont);
    GL_ResetState();
}

} // namespace

void HpBars::Receive(const uint8_t* msg, int size)
{
    if (!msg || size < 8) return;
    if (msg[0] != 0xC1 || msg[2] != 0xF3 || msg[3] != 0xF7) return;
    const int index = ((msg[4] << 8) | msg[5]) & 0x7FFF;
    const uint16_t perMille = (uint16_t)((msg[6] << 8) | msg[7]);
    Stamp(index, perMille, NowMs());
}

bool HpBars::HasFresh(const void* entity)
{
    if (!entity) return false;
    const EntityView view(entity);
    if (!view.IsActive()) return false;
    return RecordFresh(s_Records[view.NetworkKey() & 0x7FFF], NowMs());
}

void Render_HpBars(void)
{
    if (SceneFlag != 5) return;
    if (!CharactersClient) return;

    const uint32_t now = NowMs();
    TrackHero(now);

    const BYTE* base = (const BYTE*)(uintptr_t)CharactersClient;
    bool drewAny = false;

    for (int i = 0; i < EntityView::Capacity; ++i) {
        const BYTE* ent = base + i * CHARACTER_STRIDE;
        const EntityView view(ent);
        if (!view.IsActive()) continue;
        const BYTE kind = view.Kind();
        if (kind != EntityView::PlayerKind && kind != EntityView::MonsterKind) continue;
        if (kind == EntityView::MonsterKind && view.MonsterIndex() == EntityView::SoccerBall) continue;

        const HpRecord& rec = s_Records[view.NetworkKey() & 0x7FFF];
        if (!RecordFresh(rec, now)) continue;

        // Fade-out tail: full alpha until HPBAR_FADE_OUT_MS before
        // the deadline, then a linear fade to gone.
        const uint32_t age = now - rec.lastTick;
        float alpha = 1.0f;
        if (age > HpBars::HPBAR_FADE_MS - HpBars::HPBAR_FADE_OUT_MS) {
            alpha = 1.0f - (float)(age - (HpBars::HPBAR_FADE_MS - HpBars::HPBAR_FADE_OUT_MS)) /
                           (float)HpBars::HPBAR_FADE_OUT_MS;
        }

        // Same projection + distance scale as the stock boss bar
        // (CHealthBar::DrawViewport): anchor 100 world units above
        // the head, size follows how tall 100 units measure here.
        float position[3] = { view.X(), view.Y(), view.Z() + view.Height() + 100.0f };
        int x, y;
        Camera_ProjectWorldToScreen(position, &x, &y);
        float above[3] = { position[0], position[1], position[2] + 100.0f };
        int ax, ay;
        Camera_ProjectWorldToScreen(above, &ax, &ay);
        const float span = std::sqrt((float)((ax - x) * (ax - x) + (ay - y) * (ay - y)));
        float scale = span / 31.0f;
        scale = scale < 0.4f ? 0.4f : scale > 1.0f ? 1.0f : scale;
        const int width = (int)(64.0f * scale + 0.5f);
        const int height = (int)(5.0f * scale + 0.5f) < 3 ? 3 : (int)(5.0f * scale + 0.5f);
        const int left = x - width / 2;

        const float frac = rec.perMille / 1000.0f;
        // Green -> yellow -> red as the fraction falls.
        float r, g;
        if (frac > 0.5f) { r = (1.0f - frac) * 2.0f * 0.9f; g = 0.85f; }
        else             { r = 0.95f;                        g = frac * 2.0f * 0.85f; }

        EnableAlphaTest(true);
        Gfx::Color4f(0.0f, 0.0f, 0.0f, 0.65f * alpha);
        GL_DrawRect((float)(left - 1), (float)(y - 1), (float)(width + 2), (float)(height + 2));
        Gfx::Color4f(0.05f, 0.05f, 0.05f, 0.55f * alpha);
        GL_DrawRect((float)left, (float)y, (float)width, (float)height);
        if (rec.perMille > 0) {
            Gfx::Color4f(r, g, 0.1f, alpha);
            GL_DrawRect((float)(left + 1), (float)(y + 1),
                        (float)(width - 2) * frac, (float)(height - 2));
        }
        Gfx::Color4f(1.0f, 1.0f, 1.0f, 1.0f);
        GL_ResetState();
        drewAny = true;

        if (kind == EntityView::MonsterKind && IsBossClass(view.MonsterIndex())) {
            DrawPercentText(x, y - 11, rec.perMille);
        }
    }
    (void)drewAny;
}
