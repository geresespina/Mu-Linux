#include "stdafx.h"
#include "UI/BoostStrip.h"
#include "Net/BoostState.h"
#include "Net/Recv/NetRecv.h"
#include "Render/Gfx.h"
#include "Platform/Text.h"
#include "Platform/ITimer.h"
#include "Core/Font.h"
#include "Core/Window.h"
#include "UI/PanelAnchor.h"
#include "Game/HeroVitals.h"
#include "globals.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

CBoostStrip gBoostStrip;

// MEAMU BOOST STRIP -- see UI/BoostStrip.h + Net/BoostState.h.
// Parse discipline follows MapTrack (F3/F6) and the Train 8
// fail-safe pattern: ANY malformation leaves the previous
// snapshot untouched and draws nothing new; nothing here can
// crash on a short or hostile packet.

namespace {

// Flag bits (wire byte +5) -- Net/BoostState.h.
constexpr unsigned char kFlagJinchuriki = 0x01;
constexpr unsigned char kFlagForm = 0x02;
constexpr unsigned char kFlagSage = 0x04;
constexpr unsigned char kFlagMangekyou = 0x08;
constexpr unsigned char kFlagPet = 0x10;
constexpr unsigned char kFlagWar = 0x20;

// Layout (logical 640x480 HUD space, the HUD_Pass5 geometry):
// the strip's bottom edge sits 4 px above the bottom frame
// (frame top y=432), and its left edge aligns with the HP orb
// (x=97..150, y=432..480) inside the central 4:3 area, so it
// tracks the orb at 640x480 and on wide screens alike and can
// never overlap the orb or the skill bar (which lives inside
// the frame / stacks upward from y=370 at the CENTER -- the
// strip is bottom-anchored to the frame, left-anchored to the
// orb; on a full 60-skill grid the lowest grid row is y=370,
// still 36 px above the strip's top at its tallest).
constexpr int kStripBottom = 428;
constexpr int kOrbX = 97;
constexpr int kChipGap = 4;
constexpr int kFactorH = 22;
constexpr int kHeadlineH = 26;
constexpr int kIconW = 16, kIconH = 18;

// Mode/power skill icons -- the BuffBar's DrawSkillIcon logic
// verbatim (same textures, same Train 6 allocator-safe slots:
// Mangekyou 299, Sage 308, Jinchuriki 309, Biju 262; every other
// skill reads the 298 atlas; the 62..71 powers draw the BijuPowers
// atlas (0x102) instead -- Train 2 combiner fix.
void DrawSkillIcon(int skill, float x, float y)
{
    if (skill == 57) { GL_DrawTexture(0x0ca, x, y, (float)kIconW, (float)kIconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1); return; }
    if (skill == 58) { GL_DrawTexture(299, x, y, (float)kIconW, (float)kIconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1); return; }
    if (skill == 59) { GL_DrawTexture(0x134, x, y, (float)kIconW, (float)kIconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1); return; }
    if (skill == 60) { GL_DrawTexture(0x135, x, y, (float)kIconW, (float)kIconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1); return; }
    if (skill == 61) { GL_DrawTexture(0x106, x, y, (float)kIconW, (float)kIconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1); return; }
    // MEAMU 2026-11-18 (Train 3): same mapping as BuffBar -- 72 Biju
    // Bond -> Jinchuriki texture, 73 Chakra Cloak -> 0x0c8, 74
    // Perfect Susanoo -> Biju texture, 75 Dragon Sage Mode -> 0x0c9.
    if (skill == 72) { GL_DrawTexture(0x135, x, y, (float)kIconW, (float)kIconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1); return; }
    if (skill == 76) { GL_DrawTexture(0x135, x, y, (float)kIconW, (float)kIconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1); return; }
    if (skill == 73) { GL_DrawTexture(0x0c8, x, y, (float)kIconW, (float)kIconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1); return; }
    if (skill == 74) { GL_DrawTexture(0x106, x, y, (float)kIconW, (float)kIconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1); return; }
    if (skill == 75) { GL_DrawTexture(0x0c9, x, y, (float)kIconW, (float)kIconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1); return; }
    if (skill >= 62 && skill <= 71) {
        // MEAMU Train 2 combiner: beast actives draw their cell of the
        // BijuPowers atlas (bitmap 0x102, 5x2 grid) -- the stock 298 atlas
        // has no cells for 62..71 (Biju Powers bitmap audit, 2026-11-17).
        const int cell = skill - 62;
        const float cellU = (float)(cell % 5) * 0.2f;
        const float cellV = (float)(cell / 5) * 0.5f;
        GL_DrawTexture(0x102, x, y, (float)kIconW, (float)kIconH, cellU + 0.002f, cellV + 0.002f, 0.196f, 0.496f, 1, 1);
        return;
    }
    const int index = skill - 1;
    const float u = (float)((index % 8) * 32) / 256.0f;
    const float v = (float)((index / 8) * 36) / 256.0f;
    GL_DrawTexture(298, x, y, (float)kIconW, (float)kIconH, u, v, 32.0f / 256.0f, 35.0f / 256.0f, 1, 1);
}

// "x3.25" / "x2.5" / "x2" -- two decimals max, trailing zeros
// trimmed (the amendment's format law; one formatter for the
// headline and the factor chips alike). The multiplier sign is
// the Latin-1 'x' drawn as U+00D7; the client text path is
// UTF-8 throughout (ClientText).
void FormatMul(int mulX1000, char* out, size_t size)
{
    const int whole = mulX1000 / 1000;
    const int frac = mulX1000 % 1000;
    if (frac == 0) {
        Platform::TextFormatSecure(out, size, "\xC3\x97%d", whole);
        return;
    }
    char digits[4];
    Platform::TextFormatSecure(digits, sizeof(digits), "%03d", frac);
    int len = 3;
    while (len > 0 && digits[len - 1] == '0') digits[--len] = 0;
    Platform::TextFormatSecure(out, size, "\xC3\x97%d.%s", whole, digits);
}

// Text width in logical (640-space) units, measured with the
// currently selected font (the EventTimer conversion).
int TextWidthLogical(const char* text)
{
    SIZE sz = { 0, 0 };
    const int n = Platform::TextLength(text);
    gFont.MeasureText32(gFont.GetTextDC(), text, n, &sz);
    const int w = (int)gWindow.GetWidth();
    return w > 0 ? (int)((long long)sz.cx * 640 / w) : (int)sz.cx;
}

void DrawChipBox(float x, float y, float w, float h)
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

// One chip: optional skill icon + label. Returns its width.
int DrawChip(int x, int bottom, int height, int skill, const char* label, unsigned int textColor, bool bold)
{
    gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(bold ? FONT_BOLD : FONT_NORMAL));
    const int textW = TextWidthLogical(label);
    const int iconSpace = (skill > 0) ? (kIconW + 4) : 0;
    const int w = 6 + iconSpace + textW + 6;
    const int y = bottom - height;
    DrawChipBox((float)x, (float)y, (float)w, (float)height);
    if (skill > 0) {
        Gfx::Color4f(1.0f, 1.0f, 1.0f, 1.0f);
        DrawSkillIcon(skill, (float)(x + 6), (float)(y + (height - kIconH) / 2));
    }
    m_dwBackColor = 0;
    m_dwTextColor = textColor;
    RenderText(x + 6 + iconSpace, y + (bold ? 5 : 4), const_cast<char*>(label), 0, bold ? 1 : 0, nullptr);
    return w;
}

// MU_BOOSTSTRIP_SELFTEST=1: feeds synthetic packets through the
// real parser at first render, verifies store + reject paths,
// then restores the (empty) live state. The MapTrack selftest
// pattern, env-gated, dev-only.
void RunSelfTestOnce(CBoostStrip& strip)
{
    static bool done = false;
    if (done) return;
    done = true;
    if (!std::getenv("MU_BOOSTSTRIP_SELFTEST")) return;

    auto build = [](unsigned char flags, unsigned char bonus, unsigned char drain,
                    unsigned short total, unsigned char count, unsigned char skill,
                    unsigned short counter, unsigned char* pkt) {
        int size = 11 + 5 * count;
        pkt[0] = 0xC2; pkt[1] = (unsigned char)(size >> 8); pkt[2] = (unsigned char)(size & 0xFF);
        pkt[3] = 0xF3; pkt[4] = 0xF8;
        pkt[5] = flags; pkt[6] = bonus; pkt[7] = drain;
        pkt[8] = (unsigned char)(total & 0xFF); pkt[9] = (unsigned char)(total >> 8);
        pkt[10] = count;
        if (count > 0) {
            pkt[11] = skill;
            pkt[12] = (unsigned char)(counter & 0xFF); pkt[13] = (unsigned char)(counter >> 8);
            pkt[14] = 0; pkt[15] = 0;
        }
        return size;
    };

    unsigned char pkt[31];

    // Valid base packet: Jinchuriki + pet + war, bonus 15, drain 3, x2.300.
    int size = build(0x01 | 0x10 | 0x20, 15, 3, 2300, 0, 0, 0, pkt);
    strip.Receive(pkt, size);
    bool ok = strip.Have() && strip.Flags() == (0x01 | 0x10 | 0x20)
        && strip.WarBonusPct() == 15 && strip.DrainPctPerSec() == 3
        && strip.TotalMulX1000() == 2300 && strip.PowerCount() == 0;

    // Malformed: bad subcode must be dropped, store untouched.
    pkt[4] = 0xF7;
    strip.Receive(pkt, size);
    pkt[4] = 0xF8;
    ok = ok && strip.TotalMulX1000() == 2300 && strip.Flags() == (0x01 | 0x10 | 0x20);

    // Malformed: count 5 (> 4) -- rejected wholesale.
    size = build(0x01, 0, 5, 2000, 5, 70, 3, pkt);
    strip.Receive(pkt, size);
    ok = ok && strip.TotalMulX1000() == 2300 && strip.PowerCount() == 0;

    // Malformed: truncated -- rejected.
    strip.Receive(pkt, 8);
    ok = ok && strip.TotalMulX1000() == 2300;

    // Malformed: power skill outside 62..71 -- rejected.
    size = build(0x02, 0, 5, 1250, 1, 41, 3, pkt);
    strip.Receive(pkt, size);
    ok = ok && strip.TotalMulX1000() == 2300 && strip.PowerCount() == 0;

    // Valid power packet: form + one entry (skill 70, counter 3), drain 4.
    size = build(0x02, 0, 4, 1250, 1, 70, 3, pkt);
    strip.Receive(pkt, size);
    ok = ok && strip.Flags() == 0x02 && strip.TotalMulX1000() == 1250 && strip.DrainPctPerSec() == 4
        && strip.PowerCount() == 1 && strip.PowerSkill(0) == 70 && strip.PowerCounter(0) == 3;

    NetLog("NET:  BoostStrip SELFTEST %s", ok ? "PASS" : "FAIL");
    strip.Clear();
}

} // namespace

void CBoostStrip::Receive(const unsigned char* msg, int size)
{
    // C2 sizeHi sizeLo F3 F8 payload... (Net/BoostState.h)
    if (!msg || size < 11) return;
    if (msg[0] != 0xC2 || msg[3] != 0xF3 || msg[4] != 0xF8) return;
    const int declared = (msg[1] << 8) | msg[2];
    if (declared != size) return;
    const int count = msg[10];
    if (count > MaxPowers) return;
    if (size != 11 + 5 * count) return;

    Power parsed[MaxPowers] = {};
    for (int i = 0; i < count; ++i) {
        const unsigned char* e = msg + 11 + 5 * i;
        if (e[0] < 62 || e[0] > 71) return; // power entries are the 62..71 actives only
        parsed[i].skill = e[0];
        parsed[i].counter = (unsigned short)(e[1] | (e[2] << 8));
        parsed[i].aux = (unsigned short)(e[3] | (e[4] << 8));
    }

    m_Flags = msg[5];
    m_WarBonusPct = msg[6];
    m_DrainPctPerSec = msg[7];
    m_TotalMulX1000 = (unsigned short)(msg[8] | (msg[9] << 8));
    for (int i = 0; i < MaxPowers; ++i) m_Powers[i] = parsed[i];
    m_PowerCount = count;
    m_Have = true;
}

void CBoostStrip::Clear()
{
    m_Have = false;
    m_Flags = 0;
    m_WarBonusPct = 0;
    m_DrainPctPerSec = 0;
    m_TotalMulX1000 = 1000;
    m_PowerCount = 0;
}

void CBoostStrip::Render()
{
    RunSelfTestOnce(*this);
    if (SceneFlag != 5 || !m_Have) return;

    const bool form = (m_Flags & kFlagForm) != 0;
    const bool jinchuriki = (m_Flags & kFlagJinchuriki) != 0;
    const bool sage = (m_Flags & kFlagSage) != 0;
    const bool mangekyou = (m_Flags & kFlagMangekyou) != 0;
    const bool war = (m_Flags & kFlagWar) != 0;

    // Empty-state rule: product exactly x1.000, no form, no
    // factor, no drain, no powers = no strip at all. (A pet
    // alone shows nothing since the drain ruling: the drain
    // chip keys off the drain field, which is 0 with no beast
    // mode running.)
    const bool headline = form || m_TotalMulX1000 != 1000;
    if (!headline && !jinchuriki && !form && !sage && !mangekyou && !war && m_DrainPctPerSec == 0 && m_PowerCount == 0) return;

    int x = gWindow.PanelOrigin(Ui::WindowLeft) + kOrbX;

    // Headline FIRST, larger than the factor chips. In form the
    // headline reads FORM (the form is not a multiplier); the
    // product field stays live for the drop back to normal.
    if (headline) {
        char label[32];
        if (form) {
            Platform::TextFormatSecure(label, sizeof(label), "FORM");
        } else {
            char mul[16];
            FormatMul(m_TotalMulX1000, mul, sizeof(mul));
            Platform::TextFormatSecure(label, sizeof(label), "TOTAL %s", mul);
        }
        x += DrawChip(x, kStripBottom, kHeadlineH, 0, label, 0xFFFFC040u, true) + kChipGap;
    }

    if (jinchuriki) {
        char mul[16];
        FormatMul(2000, mul, sizeof(mul));
        x += DrawChip(x, kStripBottom, kFactorH, 60, mul, 0xFFE6E6E6u, false) + kChipGap;
    }

    if (form) {
        // The pool % is read from the hero's live vitals: while
        // transformed they ARE the form pool (see the wire spec).
        const unsigned int maxLife = gHeroVitals.GetMax(VITAL_LIFE);
        const unsigned int curLife = gHeroVitals.GetCurrent(VITAL_LIFE);
        char label[16];
        if (maxLife > 0) {
            Platform::TextFormatSecure(label, sizeof(label), "%u%%", (unsigned int)((unsigned long long)curLife * 100 / maxLife));
        } else {
            Platform::TextFormatSecure(label, sizeof(label), "--%%");
        }
        x += DrawChip(x, kStripBottom, kFactorH, 61, label, 0xFFE6E6E6u, false) + kChipGap;
    }

    if (sage || mangekyou) {
        char mul[16];
        FormatMul(1250, mul, sizeof(mul));
        x += DrawChip(x, kStripBottom, kFactorH, sage ? 59 : 58, mul, 0xFFE6E6E6u, false) + kChipGap;
    }

    if (war) {
        char label[16];
        Platform::TextFormatSecure(label, sizeof(label), "WAR +%d%%", (int)m_WarBonusPct);
        x += DrawChip(x, kStripBottom, kFactorH, 0, label, 0xFF8FD8FFu, false) + kChipGap;
    }

    if (m_DrainPctPerSec > 0) {
        // Mode drain: shown while a beast mode runs (the server
        // sends 0 otherwise); the %/s is the server's live drain
        // config for whichever mode is active -- never a
        // client-side constant.
        char label[16];
        Platform::TextFormatSecure(label, sizeof(label), "-%d%%/s", (int)m_DrainPctPerSec);
        x += DrawChip(x, kStripBottom, kFactorH, 0, label, 0xFFFF7070u, false) + kChipGap;
    }

    // Biju Powers entries (always empty until that build lands;
    // the draw is already generic: icon + counter).
    for (int i = 0; i < m_PowerCount; ++i) {
        char label[16];
        Platform::TextFormatSecure(label, sizeof(label), "%d", (int)m_Powers[i].counter);
        x += DrawChip(x, kStripBottom, kFactorH, m_Powers[i].skill, label, 0xFFE6E6E6u, false) + kChipGap;
    }

    gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_NORMAL));
    Gfx::Color4f(1.0f, 1.0f, 1.0f, 1.0f);
}

// F3/F8 dispatch entry (NetRecv_F3 in Recv_Character.cpp) -- the
// same forward-declared free-function pattern as MapTrack_RecvF6.
void BoostState_RecvF8(const BYTE* Msg, int Size)
{
    gBoostStrip.Receive((const unsigned char*)Msg, Size);
}
