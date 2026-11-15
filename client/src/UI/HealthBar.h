#pragma once
#include "Net/Protocol/GameServerProtocol.h"

class CHealthBar
{
public:
    void Clear();
    void Remove(WORD index);
    bool Receive(const BYTE* packet, int size);
    const Proto::PMSG_HEALTH_BAR* Find(WORD index, BYTE type) const;
    void DrawViewport() const;
    bool DrawSelected(const BYTE* entity) const;

private:
    const Proto::PMSG_HEALTH_BAR* FindEntity(const BYTE* entity) const;
    Proto::PMSG_HEALTH_BAR m_Entries[400]{};
    int m_Count = 0;
};

extern CHealthBar gHealthBar;

// MEAMU: per-frame entry for the env-gated Biju gate check
// (MU_HEALTHBAR_SELFTEST=1); a no-op unless the env var is set.
void HealthBar_SelfTestTick();
