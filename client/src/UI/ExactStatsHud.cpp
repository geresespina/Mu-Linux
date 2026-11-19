#include "stdafx.h"
#include "UI/ExactStatsHud.h"
#include "Net/ExactStats.h"
#include "Render/Gfx.h"
#include "Platform/Text.h"
#include "Core/Font.h"
#include "Core/Window.h"
#include "UI/PanelAnchor.h"
#include "globals.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// MEAMU TRAIN 11 -- see UI/ExactStatsHud.h. Rendering helpers are
// local copies of the Boost Strip's chip language (same box, same
// text path) so the slim line and the strip read as one instrument
// without coupling the two modules.

namespace {

constexpr int kLineBottom = 398; // Boost Strip bottom is 428, its
                                 // tallest chip top is ~402: the
                                 // slim line sits directly above it.
constexpr int kOrbX = 97;
constexpr int kLineH = 18;
constexpr int kChipGap = 4;

int TextWidthLogical(const char* text)
{
    SIZE sz = { 0, 0 };
    const int n = Platform::TextLength(text);
    gFont.MeasureText32(gFont.GetTextDC(), text, n, &sz);
    const int w = (int)gWindow.GetWidth();
    return w > 0 ? (int)((long long)sz.cx * 640 / w) : (int)sz.cx;
}

void DrawBox(float x, float y, float w, float h)
{
    EnableAlphaTest(true);
    EnableAlphaBlend();
    Gfx::Color4f(0.0f, 0.0f, 0.0f, 0.55f);
    GL_DrawRect(x, y, w, h);
    Gfx::Color4f(1.0f, 1.0f, 1.0f, 0.28f);
    GL_DrawRect(x, y, w, 1.0f);
    GL_DrawRect(x, y + h - 1.0f, w, 1.0f);
    GL_DrawRect(x, y, 1.0f, h);
    GL_DrawRect(x + w - 1.0f, y, 1.0f, h);
    Gfx::Color4f(1.0f, 1.0f, 1.0f, 1.0f);
    GL_ResetState();
}

// One slim chip: label only. Returns its width.
int DrawSlimChip(int x, int bottom, const char* label, unsigned int textColor)
{
    gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_NORMAL));
    const int textW = TextWidthLogical(label);
    const int w = 6 + textW + 6;
    const int y = bottom - kLineH;
    DrawBox((float)x, (float)y, (float)w, (float)kLineH);
    m_dwBackColor = 0;
    m_dwTextColor = textColor;
    RenderText(x + 6, y + 3, const_cast<char*>(label), 0, 0, nullptr);
    return w;
}

void FormatNum(int32_t v, char* buf, size_t cap)
{
    // Thousands-separated, sign-aware.
    char digits[24];
    const bool neg = v < 0;
    uint32_t u = neg ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    std::snprintf(digits, sizeof(digits), "%u", (unsigned)u);
    const int n = (int)std::strlen(digits);
    size_t out = 0;
    if (neg && out + 1 < cap) buf[out++] = '-';
    for (int i = 0; i < n; ++i) {
        if (i > 0 && ((n - i) % 3) == 0) {
            if (out + 1 >= cap) break;
            buf[out++] = ',';
        }
        if (out + 1 >= cap) break;
        buf[out++] = digits[i];
    }
    buf[out] = '\0';
}

void FormatPctX10(int32_t x10, char* buf, size_t cap)
{
    if (x10 % 10 == 0) {
        Platform::TextFormatSecure(buf, cap, "%d%%", (int)(x10 / 10));
    } else {
        Platform::TextFormatSecure(buf, cap, "%d.%d%%", (int)(x10 / 10), (int)(x10 % 10 < 0 ? -(x10 % 10) : x10 % 10));
    }
}

void FormatMulPct(int32_t pct, char* buf, size_t cap)
{
    // 100 -> "x1", 125 -> "x1.25", 1953 -> "x19.53"
    const int whole = (int)(pct / 100);
    int frac = (int)(pct % 100);
    if (frac == 0) {
        Platform::TextFormatSecure(buf, cap, "\xC3\x97%d", whole);
        return;
    }
    char digits[4];
    Platform::TextFormatSecure(digits, sizeof(digits), "%02d", frac);
    int len = 2;
    while (len > 0 && digits[len - 1] == '0') digits[--len] = 0;
    Platform::TextFormatSecure(buf, cap, "\xC3\x97%d.%s", whole, digits);
}

// MU_EXACTSTATS_SELFTEST=1: feed a synthetic packet through the
// real Receive path at first render, verify the store, restore.
// The BoostStrip selftest pattern, env-gated, dev-only.
void RunSelfTestOnce()
{
    static bool done = false;
    if (done) return;
    done = true;
    if (!std::getenv("MU_EXACTSTATS_SELFTEST")) return;
    uint8_t pkt[ExactStats::kPacketSize] = { 0 };
    pkt[0] = 0xC1; pkt[1] = (uint8_t)ExactStats::kPacketSize;
    pkt[2] = 0xF3; pkt[3] = 0xFB;
    auto put = [&](int off, int32_t v) {
        pkt[off] = (uint8_t)(v & 0xFF); pkt[off + 1] = (uint8_t)((v >> 8) & 0xFF);
        pkt[off + 2] = (uint8_t)((v >> 16) & 0xFF); pkt[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    };
    put(4, 111); put(8, 222); put(20, 333); put(32, 444); put(60, 500); put(64, 600000);
    ExactStats::Receive(pkt, sizeof(pkt));
    const ExactStats::Stats& s = ExactStats::Get();
    if (!ExactStats::HasStats() || s.physiMin != 111 || s.physiMax != 222 ||
        s.physiSpeed != 333 || s.defense != 444 || s.formMax != 600000) {
        Platform::TextFormatSecure(const_cast<char*>(""), 0, "");
    }
    ExactStats::Clear();
}

void RenderSheet(const ExactStats::Stats& s)
{
    constexpr int kW = 304, kH = 268;
    const int ox = gWindow.PanelOrigin(Ui::WindowLeft) + (640 - kW) / 2;
    const int oy = 96;
    DrawBox((float)ox, (float)oy, (float)kW, (float)kH);

    gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_BOLD));
    m_dwBackColor = 0;
    m_dwTextColor = 0xFFFFC040u;
    RenderText(ox + 12, oy + 8, const_cast<char*>("EXACT STATS"), 0, 1, nullptr);
    gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_NORMAL));
    m_dwTextColor = 0xFF9A9A9Au;
    RenderText(ox + kW - 52, oy + 9, const_cast<char*>("F6"), 0, 0, nullptr);

    char v[64], a[32], b[32];
    int row = 0;
    auto line = [&](const char* label, const char* value) {
        const int y = oy + 32 + row * 18;
        ++row;
        gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_NORMAL));
        m_dwBackColor = 0;
        m_dwTextColor = 0xFFB8B8B8u;
        RenderText(ox + 12, y, const_cast<char*>(label), 0, 0, nullptr);
        m_dwTextColor = 0xFFFFFFFFu;
        RenderText(ox + 150, y, const_cast<char*>(value), 0, 0, nullptr);
    };

    FormatNum(s.physiMin, a, sizeof(a)); FormatNum(s.physiMax, b, sizeof(b));
    Platform::TextFormatSecure(v, sizeof(v), "%s - %s", a, b);
    line("DMG", v);

    FormatNum(s.magicMin, a, sizeof(a)); FormatNum(s.magicMax, b, sizeof(b));
    Platform::TextFormatSecure(v, sizeof(v), "%s - %s", a, b);
    line("MAGIC DMG", v);

    FormatNum(s.physiSpeed, a, sizeof(a));
    line("ATK SPD", a);

    FormatNum(s.magicSpeed, a, sizeof(a));
    line("MAGIC SPD", a);

    FormatNum(s.defense, a, sizeof(a));
    line("DEF", a);

    FormatNum(s.attackRate, a, sizeof(a));
    line("ATK RATE PvP", a);

    FormatNum(s.defenseRate, a, sizeof(a));
    line("DEF RATE PvP", a);

    FormatPctX10(s.critX10, a, sizeof(a));
    line("CRIT %", a);

    FormatPctX10(s.excX10, a, sizeof(a));
    line("EXC %", a);

    FormatMulPct(s.totalMulPct, a, sizeof(a));
    line("TOTAL", a);

    Platform::TextFormatSecure(v, sizeof(v), "+%d%%", (int)s.clanBonusPct);
    line("CLAN BONUS %", v);

    Platform::TextFormatSecure(v, sizeof(v), "%d%%/s", (int)s.drainPct);
    line("DRAIN %/s", v);

    if (s.formMax > 0) {
        FormatNum(s.formCur, a, sizeof(a)); FormatNum(s.formMax, b, sizeof(b));
        Platform::TextFormatSecure(v, sizeof(v), "%s / %s", a, b);
    } else {
        Platform::TextFormatSecure(v, sizeof(v), "-");
    }
    line("FORM POOL", v);

    FormatNum(s.moveSpeed, a, sizeof(a));
    line("MOVE SPD", a);

    gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_NORMAL));
    Gfx::Color4f(1.0f, 1.0f, 1.0f, 1.0f);
}

} // namespace

namespace ExactStatsHud {

void Render()
{
    RunSelfTestOnce();
    if (SceneFlag != 5 || !ExactStats::HasStats()) return;

    // F6 toggles the full sheet (key census: VK_F6 has no other
    // consumer in this client; 'C' is the stock character window).
    if (PressKey(0x75)) ExactStats::ToggleSheet();

    const ExactStats::Stats& s = ExactStats::Get();

    // ── Slim line (always on) ──────────────────────────────────
    int x = gWindow.PanelOrigin(Ui::WindowLeft) + kOrbX;
    const int lineLeft = x;
    char label[96], a[32], b[32];

    FormatNum(s.physiMin, a, sizeof(a)); FormatNum(s.physiMax, b, sizeof(b));
    Platform::TextFormatSecure(label, sizeof(label), "DMG %s-%s", a, b);
    x += DrawSlimChip(x, kLineBottom, label, 0xFFFFFFFFu) + kChipGap;

    FormatNum(s.physiSpeed, a, sizeof(a));
    Platform::TextFormatSecure(label, sizeof(label), "SPD %s", a);
    x += DrawSlimChip(x, kLineBottom, label, 0xFFE6E6E6u) + kChipGap;

    FormatNum(s.defense, a, sizeof(a));
    Platform::TextFormatSecure(label, sizeof(label), "DEF %s", a);
    x += DrawSlimChip(x, kLineBottom, label, 0xFFE6E6E6u) + kChipGap;

    if (s.formMax > 0) {
        FormatNum(s.formCur, a, sizeof(a)); FormatNum(s.formMax, b, sizeof(b));
        Platform::TextFormatSecure(label, sizeof(label), "FORM %s/%s", a, b);
        x += DrawSlimChip(x, kLineBottom, label, 0xFFFFC040u) + kChipGap;
    }

    if (s.drainPct > 0) {
        Platform::TextFormatSecure(label, sizeof(label), "-%d%%/s", (int)s.drainPct);
        x += DrawSlimChip(x, kLineBottom, label, 0xFFFF7070u) + kChipGap;
    }

    const int lineRight = x - kChipGap;

    // Click on the slim line toggles the sheet (mouse coords are
    // in the same logical HUD space as the line's rect).
    if (MouseLButtonPush && MouseX >= lineLeft && MouseX < lineRight &&
        MouseY >= kLineBottom - kLineH && MouseY < kLineBottom) {
        ExactStats::ToggleSheet();
        MouseLButtonPush = 0;
    }

    // ── Full sheet (toggle) ────────────────────────────────────
    if (ExactStats::SheetOpen()) RenderSheet(s);

    gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_NORMAL));
    Gfx::Color4f(1.0f, 1.0f, 1.0f, 1.0f);
}

} // namespace ExactStatsHud
