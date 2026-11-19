#pragma once
#include <cstddef>

// MEAMU BOOST STRIP (2026-11-17, G's order): a slim row of state
// chips above the HP orb showing the hero's live boosts -- the
// headline TOTAL damage multiplier plus one chip per active
// factor. Every value arrives from the server in the F3/F8
// BoostState packet (see Net/BoostState.h for the wire spec), so
// the strip is never client-guessed. The packet is a wholesale
// snapshot re-sent on change only (login / mode toggle / pet
// equip / war-bonus step), so there is no freshness timeout: the
// last snapshot stands until the next one or Clear() (character
// change, CNetwork::ResetCharacterData).
//
// MEAMU TRAIN 8 ADDENDUM (2026-11-19, G: "this is incomplete. i
// dont see all my buffs calculated in"): the F3/F8 snapshot
// predates the all-transforms-stack law -- it carries no Cloak
// (x5) or Dragon Sage (x1.25) flag, collapses Mangekyou + Sage
// into a single x1.25 in its total, and its drain byte holds
// one mode's rate, never the sum. The factor chips, the TOTAL
// and the drain are therefore COMPOSED client-side from the
// hero's own state bitmap (entity+120 -- the same DWORD every
// mode aura keys off; bits 0x0400/0x0800/0x1000/0x2000/0x4000/
// 0x8000 name the six modes unambiguously) via the pure
// BoostCompose helpers below. The wire still supplies the war
// bonus %, the Biju Powers entries, and the fallback values
// when the bitmap cannot be read. Composition law (GS truth,
// EffectManager::RecomputeMulDamagePct + Attack.cpp): the five
// non-form mode multipliers MULTIPLY -- Mangekyou x1.25, Sage
// x1.25, Jinchuriki x2, Dragon Sage x1.25, Cloak x5, each
// counted on its own -- and the war bonus multiplies on top;
// blood price SUMS at 3%/s per active transform (the shared
// form bit counts once) + 8%/s for the Cloak.
namespace BoostCompose
{
    // The composed product, x1000, rounded to the nearest unit
    // (the true product in double; the GS raw pipeline stores
    // it as an integer percent and the form funnel composes it
    // in double, so this display sits within 0.001 of the
    // funnel's number at every stack depth).
    inline int TotalMulX1000(bool mangekyou, bool sage, bool jinchuriki,
                             bool dragonSage, bool cloak, int warBonusPct)
    {
        double mul = 1.0;
        if (mangekyou) mul *= 1.25;
        if (sage) mul *= 1.25;
        if (jinchuriki) mul *= 2.0;
        if (dragonSage) mul *= 1.25;
        if (cloak) mul *= 5.0;
        if (warBonusPct > 0) mul *= (100.0 + (double)warBonusPct) / 100.0;
        return (int)(mul * 1000.0 + 0.5);
    }

    // The summed blood price, %/s (G's 2026-11-19 rates: 3%
    // per transform, 8% the Cloak; the proven maximum is
    // 5 x 3 + 8 = 23%/s -- the two forms share one body, so
    // the form bit can only ever contribute one 3%).
    inline int DrainPctPerSec(bool mangekyou, bool sage, bool jinchuriki,
                              bool form, bool dragonSage, bool cloak)
    {
        int drain = 0;
        if (mangekyou) drain += 3;
        if (sage) drain += 3;
        if (jinchuriki) drain += 3;
        if (form) drain += 3;
        if (dragonSage) drain += 3;
        if (cloak) drain += 8;
        return drain;
    }
}
class CBoostStrip {
public:
    void Receive(const unsigned char* packet, int size);
    void Clear();
    void Render();

    // Read-only snapshot accessors (the env-gated parser selftest
    // asserts through these; nothing else needs them yet).
    bool Have() const { return m_Have; }
    unsigned char Flags() const { return m_Flags; }
    unsigned char WarBonusPct() const { return m_WarBonusPct; }
    unsigned char DrainPctPerSec() const { return m_DrainPctPerSec; }
    unsigned short TotalMulX1000() const { return m_TotalMulX1000; }
    int PowerCount() const { return m_PowerCount; }
    unsigned char PowerSkill(int i) const { return (i >= 0 && i < m_PowerCount) ? m_Powers[i].skill : 0; }
    unsigned short PowerCounter(int i) const { return (i >= 0 && i < m_PowerCount) ? m_Powers[i].counter : 0; }

private:
    struct Power {
        unsigned char skill = 0;   // 62..71 (Biju Powers actives)
        unsigned short counter = 0;
        unsigned short aux = 0;
    };
    static constexpr int MaxPowers = 4;

    bool m_Have = false;
    unsigned char m_Flags = 0;
    unsigned char m_WarBonusPct = 0;
    unsigned char m_DrainPctPerSec = 0;
    unsigned short m_TotalMulX1000 = 1000;
    Power m_Powers[MaxPowers] = {};
    int m_PowerCount = 0;
};
extern CBoostStrip gBoostStrip;
