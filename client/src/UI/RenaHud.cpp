// RenaHud.cpp — MEAMU Train 8: Rena balance on the main UI bar.
// See RenaHud.h for the design + the packet reconcile block.
//
// Self-test: MU_RENAHUD_SELFTEST=1 fabricates a balance of 12,345
// at the first render (no server push needed) and prints a PASS
// line once the number has been drawn — GG can eyeball the HUD on
// any server build before the GS counter lands.

#include "stdafx.h"
#include "UI/RenaHud.h"
#include "Render/Gfx.h"
#include "Platform/Text.h"
#include "globals.h"
#include "functions.h"

#include <cstdlib>
#include <cstring>

namespace RenaHud {
namespace {

uint32_t s_Balance = 0;
bool     s_HasBalance = false;
bool     s_SelfTestFired = false;

void SelfTestOnce()
{
    if (s_SelfTestFired) return;
    s_SelfTestFired = true;
    if (!std::getenv("MU_RENAHUD_SELFTEST")) return;
    if (!s_HasBalance) {
        s_Balance = 12345;
        s_HasBalance = true;
    }
    char text[24];
    std::printf("RENAHUD SELFTEST: balance fabricated at %u (renders as \"%s\")\n",
                (unsigned)s_Balance,
                FormatBalance(s_Balance, text, sizeof(text)));
}

} // namespace

void Receive(const uint8_t* msg, int size)
{
    // Everything wire-shaped is decided by ParseBalancePacket
    // (RenaHud.h), against the constants in the header's reconcile
    // block — GG's one-line fix lands there and only there.
    uint32_t value = 0;
    if (!ParseBalancePacket(msg, size, value)) return;
    s_Balance = value;
    s_HasBalance = true;
}

bool HasBalance() { return s_HasBalance; }

uint32_t Balance() { return s_Balance; }

void RenderNumber2D()
{
    SelfTestOnce();
    if (!s_HasBalance) return;

    char text[24];
    FormatBalance(s_Balance, text, sizeof(text));

    // GL discipline (Train 6/7 lesson — the bottom bar's textured
    // draws trust inherited state): normalize on entry, draw, and
    // leave normalized state + white colour behind, the same
    // contract Render_ChatBox_ exits with. This runs at the very
    // end of sub_4BD650, whose next action is GL_End2D (matrices
    // only — blend/colour state cannot leak into it).
    GL_ResetState();
    EnableAlphaTest(true);

    const DWORD backupTextColor = m_dwTextColor;
    const DWORD backupBackColor = m_dwBackColor;
    m_dwTextColor = 0xFFFFD98Au;   // warm gold — currency, distinct
                                   // from the white HP/MP readouts
    m_dwBackColor = 0x80000000u;   // standard label shadow

    gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_NORMAL)
                     ? gFont.GetFont(FONT_NORMAL) : gFont.GetFont(FONT_BOLD));

    // Right-align to the fixed right edge so the number never
    // jitters as digits change: measure, then place the left edge.
    const int n = Platform::TextLength(text);
    SIZE ts = { 0, 0 };
    gFont.MeasureText(gFont.GetTextDC(), text, n, &ts);
    const int w = (int)((double)ts.cx / gWindow.GetScreenRateX());
    int x = (int)kNumberRightEdge - w;
    if (x < (int)kIconX + (int)kIconSize + 2) x = (int)kIconX + (int)kIconSize + 2;

    UI_DrawText(x, (int)kNumberY, text, 0, 1, 0, sizeof(text));

    m_dwTextColor = backupTextColor;
    m_dwBackColor = backupBackColor;

    GL_ResetState();
    Gfx::Color3f(1.0f, 1.0f, 1.0f);
}

void RenderIcon3D()
{
    if (!s_HasBalance) return;
    // The Rena item's own model, through the exact entry point the
    // inventory grid and the hotbar use. This runs inside
    // Render_HotbarItems3D_'s item projection (after its Train 7
    // GL_ResetState), so no extra state handling is needed — or
    // allowed: that pass owns the projection stack.
    RenderItem3D(kIconX, kIconY, kIconSize, kIconSize,
                 kRenaItemType, 0, 0, 0, false);
    Gfx::Color3f(1.0f, 1.0f, 1.0f);
}

} // namespace RenaHud
