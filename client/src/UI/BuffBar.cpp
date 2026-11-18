#include "stdafx.h"
#include "UI/BuffBar.h"
#include "Net/Protocol/GameServerProtocol.h"
#include "Render/Gfx.h"
#include "Platform/Text.h"
#include "UI/MoveList.h"
#include "Platform/ITimer.h"
#include "Local/ClientText.h"
#include "Core/Window.h"
#include "UI/PanelAnchor.h"
#include <cstring>

CBuffBar gBuffBar;

namespace {
// Íconos del atlas de skills (bitmap 298): 8 columnas de celdas de 32x36,
// como los dibuja sub_4BB940. En la barra van más chicos, a la izquierda en la
// altura del contador de FPS (F12), cerca del borde de arriba.
constexpr int CellW = 32, CellH = 36;
constexpr int IconW = 16, IconH = 18, Gap = 2, BarY = 10, BarX = 110;

void DrawSkillIcon(int skill, float x, float y)
{
    if (skill == 58) {
        // MEAMU 2026-11-15: Mangekyou Sharingan has no cell in the
        // 256px Skill.jpg atlas (row 7 falls off the texture); it
        // draws its own texture (bitmap 299) full-frame.
        GL_DrawTexture(299, x, y, (float)IconW, (float)IconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1);
        return;
    }
    if (skill == 59) {
        // MEAMU 2026-11-17: Sage Mode -- same story as 58, its own
        // texture (the leaf mark) full-frame.
        // MEAMU 2026-11-17 (Train 6): bitmap 0x134 (308). Train 5's
        // 0x5F1 sat inside the ContentCatalog texture region (the
        // catalog allocator marches upward from 1450) and was
        // overwritten in-game; bitmap 300 is BITMAP_HIDE. See the
        // loader in Model_SkillEffects.cpp.
        GL_DrawTexture(0x134, x, y, (float)IconW, (float)IconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1);
        return;
    }
    if (skill == 60) {
        // MEAMU 2026-11-17 (Train 4): Jinchuriki Mode -- same story
        // as 58/59, its own texture full-frame.
        // MEAMU 2026-11-17 (Train 6): bitmap 0x135 (309), NOT 301 --
        // 301 (0x12d) is BITMAP_SKIN (player skin variant 0).
        GL_DrawTexture(0x135, x, y, (float)IconW, (float)IconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1);
        return;
    }
    if (skill == 61) {
        // MEAMU 2026-11-17 (Train 4): Biju Mode -- same story as
        // 58/59, its own texture full-frame.
        // MEAMU 2026-11-17 (Train 6): bitmap 0x106 (262), NOT 302 --
        // 302 (0x12e) is the player skin variant 1 texture.
        GL_DrawTexture(0x106, x, y, (float)IconW, (float)IconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1);
        return;
    }
    if (skill == 72) {
        // MEAMU 2026-11-18 (Train 3): Biju Bond -- the bond ignites
        // Jinchuriki Mode, so it draws the Jinchuriki texture (0x135).
        GL_DrawTexture(0x135, x, y, (float)IconW, (float)IconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1);
        return;
    }
    if (skill == 73) {
        // MEAMU 2026-11-18 (Train 3): Chakra Cloak -- dedicated
        // texture (bitmap 0x0c8), see Model_SkillEffects.cpp.
        GL_DrawTexture(0x0c8, x, y, (float)IconW, (float)IconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1);
        return;
    }
    if (skill == 74) {
        // MEAMU 2026-11-18 (Train 3): Perfect Susanoo shares the
        // Biju form bit (0x2000) and draws the Biju texture (0x106);
        // the model is its identity (no new icon, per design).
        GL_DrawTexture(0x106, x, y, (float)IconW, (float)IconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1);
        return;
    }
    if (skill == 75) {
        // MEAMU 2026-11-18 (Train 3): Dragon Sage Mode -- dedicated
        // texture (bitmap 0x0c9), see Model_SkillEffects.cpp.
        GL_DrawTexture(0x0c9, x, y, (float)IconW, (float)IconH, 0.0f, 0.0f, 1.0f, 1.0f, 1, 1);
        return;
    }
    if (skill >= 62 && skill <= 71) {
        // MEAMU 2026-11-18 (Train 2, Module A): Biju Powers glyphs ride
        // ONE atlas (Interface\BijuPowers.jpg -> bitmap 0x102, loaded in
        // Model_SkillEffects.cpp): 5 cols x 2 rows of 64px cells in a
        // 320x128 texture, cell index = skill - 62 (tails 1..10).
        const int cell = skill - 62;
        const float u = (float)(cell % 5) * 0.2f;
        const float v = (float)(cell / 5) * 0.5f;
        GL_DrawTexture(0x102, x, y, (float)IconW, (float)IconH, u, v,
                       0.2f, 0.5f - 1.0f / 128.0f, 1, 1);
        return;
    }
    const int index = skill - 1;
    const float u = (float)((index % 8) * CellW) / 256.0f;
    const float v = (float)((index / 8) * CellH) / 256.0f;
    GL_DrawTexture(298, x, y, (float)IconW, (float)IconH, u, v,
                   (float)CellW / 256.0f, (float)(CellH - 1) / 256.0f, 1, 1);
}

void FormatTime(unsigned long seconds, char* out, size_t size)
{
    if (seconds >= 3600)
        Platform::TextFormatSecure(out, size, "%lu:%02lu:%02lu", seconds / 3600, seconds / 60 % 60, seconds % 60);
    else
        Platform::TextFormatSecure(out, size, "%lu:%02lu", seconds / 60, seconds % 60);
}

// Lo que mejora cada efecto (índices de Effect.txt del server).
ClientTextId BonusText(int effect)
{
    switch (effect) {
        case 1: return ClientTextId::BuffDamage;
        case 2: return ClientTextId::BuffDefense;
        case 4: return ClientTextId::BuffDamageReduction;
        case 8: return ClientTextId::BuffMaxLife;
        default: return ClientTextId::Count;
    }
}

// MEAMU 2026-11-18 (Train 2, Module A): the equipped soul pet's Biju
// passive. The GS grants the beast's ACTIVE skill (62..71) on equip and
// strips it on unequip (same hook as modes 60/61), and the passive rides
// that grant -- so the hero's own skill list (CharacterAttribute: count
// at +86, skill ids at +87, 60 slots; Recv_Character.cpp F3/11 incl. the
// 0xFE/0xFF deltas) is the client-side truth for "which passive is on
// me". At most one of 62..71 can be held at a time.
// Names + effect lines: BIJU-POWERS-PLAN.md (G approved 2026-11-17),
// numbers verbatim; lines pre-split to fit the 100-byte tooltip slots.
// Two lines follow the LIVE server where it deviates from the plan
// (flagged in the Train 2 report): Steam Pressure omits the +move speed
// the GS cannot grant (build deviation D3), Octopus Grip's root is 2 s
// (D5, engine whole-second effects).
struct PassiveText {
    const char* title;
    const char* line1;
    const char* line2;
};
const PassiveText kPassiveText[10] = {
    { "Shukaku - Sand Shield",
      "A shield regrows every 12 s, absorbing 10% of your MaxLife.", "" },
    { "Matatabi - Blue Fire",
      "Hits have a 20% chance to ignite: burn 3 s at 15% of",
      "the igniting hit per tick." },
    { "Isobu - Coral Armour",
      "Melee attackers: 25% chance to be slowed 40% for 3 s",
      "and stung back for 10% of their own hit." },
    { "Son Goku - Lava Fists",
      "Hits splash 30% to enemies within 1 tile of the target",
      "and shred 10% defence for 4 s." },
    { "Kokuo - Steam Pressure",
      "Every hit you land adds a stack: +2% attack speed",
      "(max 10). Stacks decay after 4 s out of combat." },
    { "Saiken - Acid Body",
      "Melee attackers get acid: 10% of their hit per tick x3",
      "+ 25% slow. Your hits apply weaker acid (5% per tick x3)." },
    { "Chomei - Scale Powder",
      "10% chance when hit to blind the attacker (their next",
      "3 attacks miss) + 5% evasion." },
    { "Gyuki - Octopus Grip",
      "Hits have a 15% chance to bind the target (root 2 s).", "" },
    { "Kurama - Chakra Arms",
      "Your hits also swipe the nearest other enemy within",
      "2 tiles of your target for 50% of the hit." },
    { "Ten-Tails - Truth-Seeker Orbs",
      "3 orbs orbit you; each eats one incoming hit whole,",
      "then regrows in 8 s." },
};

int ActiveBijuPassiveSkill()
{
    if (!CharacterAttribute) return 0;
    const unsigned char* ca = (const unsigned char*)(uintptr_t)CharacterAttribute;
    for (int i = 0; i < 60; ++i) {
        const int skill = ca[87 + i];
        if (skill >= 62 && skill <= 71) return skill;
    }
    return 0;
}
}

void CBuffBar::Receive(const unsigned char* packet, int size)
{
    if (size < (int)sizeof(Proto::PMSG_EFFECT_INFO_SEND)) return;
    Proto::PMSG_EFFECT_INFO_SEND msg;
    std::memcpy(&msg, packet, sizeof(msg));
    int slot = 0;
    while (slot < m_Count && m_Buffs[slot].effect != msg.effect) ++slot;
    if (!msg.state) {
        if (slot == m_Count) return;
        for (int i = slot + 1; i < m_Count; ++i) m_Buffs[i - 1] = m_Buffs[i];
        --m_Count;
        return;
    }
    if (slot == m_Count) {
        if (m_Count == MaxBuffs) return;
        ++m_Count;
    }
    Buff& buff = m_Buffs[slot];
    buff = Buff{};
    buff.effect = msg.effect;
    buff.skill = msg.skill;
    buff.seconds = msg.time;
    buff.receivedAt = Platform::GetTimer().Ticks();
    std::memcpy(buff.value, msg.value, sizeof(buff.value));
    std::memcpy(buff.caster, msg.caster, sizeof(msg.caster));
    std::memcpy(buff.name, msg.name, sizeof(msg.name));
}

void CBuffBar::Clear()
{
    m_Count = 0;
}

unsigned long CBuffBar::Remaining(const Buff& buff) const
{
    const unsigned long elapsed = (Platform::GetTimer().Ticks() - buff.receivedAt) / 1000;
    return elapsed < buff.seconds ? buff.seconds - elapsed : 0;
}

void CBuffBar::Render()
{
    if (SceneFlag != 5) return;
    // MEAMU 2026-11-18 (Train 2, Module A): the Biju passive icon rides
    // the same row, after the server-fed buffs; it shows even when no
    // server buff is active (the passive is not an F3/F1 effect).
    const int passive = ActiveBijuPassiveSkill();
    int visible = 0;
    for (int i = 0; i < m_Count; ++i)
        if (!m_Buffs[i].seconds || Remaining(m_Buffs[i])) ++visible;
    if (!visible && !passive) return;

    int x = gWindow.PanelOrigin(Ui::WindowLeft) + BarX;
    const Buff* hovered = nullptr;
    int hoveredX = 0;
    int hoveredPassive = 0;
    int hoveredPassiveX = 0;
    Gfx::Color4f(1.0f, 1.0f, 1.0f, 1.0f);
    for (int i = 0; i < m_Count; ++i) {
        const Buff& buff = m_Buffs[i];
        const unsigned long left = Remaining(buff);
        if (buff.seconds && !left) continue;
        // Los últimos 10 segundos el ícono parpadea, como aviso.
        const bool blink = buff.seconds && left <= 10 && (Platform::GetTimer().Ticks() / 250) % 2;
        Gfx::Color4f(1.0f, 1.0f, 1.0f, blink ? 0.4f : 1.0f);
        if (buff.skill > 0 && buff.skill < 256)
            DrawSkillIcon(buff.skill, (float)x, (float)BarY);
        else {
            Gfx::Color4f(0.2f, 0.2f, 0.2f, 0.8f);
            GL_DrawRect((float)x, (float)BarY, (float)IconW, (float)IconH);
        }
        if (MouseX >= x && MouseX < x + IconW && MouseY >= BarY && MouseY < BarY + IconH) {
            hovered = &buff;
            hoveredX = x;
        }
        x += IconW + Gap;
    }
    if (passive) {
        Gfx::Color4f(1.0f, 1.0f, 1.0f, 1.0f);
        DrawSkillIcon(passive, (float)x, (float)BarY);
        if (MouseX >= x && MouseX < x + IconW && MouseY >= BarY && MouseY < BarY + IconH) {
            hoveredPassive = passive;
            hoveredPassiveX = x;
        }
        x += IconW + Gap;
    }
    Gfx::Color4f(1.0f, 1.0f, 1.0f, 1.0f);
    // Con el menú M abierto los íconos quedan debajo: sin tooltip.
    if (hovered && !gMoveList.IsOpen()) RenderTooltip(*hovered, hoveredX + IconW / 2, BarY + IconH + 4);
    else if (hoveredPassive && !gMoveList.IsOpen()) RenderPassiveTooltip(hoveredPassive, hoveredPassiveX + IconW / 2, BarY + IconH + 4);
}

void CBuffBar::RenderPassiveTooltip(int skill, int x, int y) const
{
    if (skill < 62 || skill > 71) return;
    const PassiveText& text = kPassiveText[skill - 62];
    char* line = lpString_07e90798;
    int count = 0;
    auto add = [&](int color, int bold, const char* str) {
        Platform::TextFormatSecure(line + count * 100, 100, "%s", str);
        DAT_07e91708[count] = color;
        DAT_07ea7b10[count] = bold;
        ++count;
    };
    add(1, 1, text.title);
    add(0, 0, text.line1);
    if (text.line2[0]) add(0, 0, text.line2);
    add(3, 0, "Biju passive - equipped soul pet");
    CharMenu_RenderTextList(x, y, count, 0, 1, 1);
}

void CBuffBar::RenderTooltip(const Buff& buff, int x, int y) const
{
    char* line = lpString_07e90798;
    int count = 0;
    auto add = [&](int color, int bold, const char* text) {
        Platform::TextFormatSecure(line + count * 100, 100, "%s", text);
        DAT_07e91708[count] = color;
        DAT_07ea7b10[count] = bold;
        ++count;
    };
    // El nombre del skill sale de skill.bmd (traducido); si no hay skill, el
    // que manda el server.
    char name[64] = {};
    if (buff.skill > 0 && buff.skill < 256) {
        int mana = 0, distance = 0, skillMana = 0;
        GetSkillInformation(buff.skill, 1, name, &mana, &distance, &skillMana);
    }
    if (!name[0]) Platform::TextFormatSecure(name, sizeof(name), "%s", buff.name);
    add(1, 1, name);

    char text[100];
    const ClientTextId bonus = BonusText(buff.effect);
    if (bonus != ClientTextId::Count) {
        Platform::TextFormatSecure(text, sizeof(text), gClientText.Get(bonus), (int)buff.value[0]);
        add(0, 0, text);
    }
    if (buff.seconds) {
        char time[16];
        FormatTime(Remaining(buff), time, sizeof(time));
        Platform::TextFormatSecure(text, sizeof(text), gClientText.Get(ClientTextId::BuffTimeLeft), time);
        add(0, 0, text);
    }
    if (buff.caster[0]) {
        Platform::TextFormatSecure(text, sizeof(text), gClientText.Get(ClientTextId::BuffCaster), buff.caster);
        add(3, 0, text);
    }
    CharMenu_RenderTextList(x, y, count, 0, 1, 1);
}
