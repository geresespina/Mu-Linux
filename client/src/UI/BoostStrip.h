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
