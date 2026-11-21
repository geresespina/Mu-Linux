#include "stdafx.h"
#include "Input/Fishing.h"
#include "functions.h"        // CreateEffect / PlayBuffer
#include "globals.h"          // Hero
#include "structs.h"
#include "Entity/EntityTypes.h"
#include "Net/Net.h"          // gNetwork

namespace {
    int s_BobberX = -1, s_BobberY = -1;

    void SparkleAtBobber()
    {
        if (s_BobberX < 0 || !Hero) return;
        OBJECT* hero = AsObject((CHARACTER*)(uintptr_t)Hero);
        float pos[3] = { (float)(s_BobberX * 100 + 50),
                         (float)(s_BobberY * 100 + 50),
                         hero->Position[2] };
        CreateEffect(1176, pos, hero->Angle, hero->Light, nullptr, nullptr,
                     (float*)(uintptr_t)0xFFFFFFFFu, nullptr, 0);
    }
}

void Fishing_Verb()
{
    const BYTE pkt[5] = { 0xC1, 0x05, 0xF3, 0xFE, 0x00 };
    gNetwork.SendC1(pkt, sizeof(pkt));
}

void Fishing_Receive(const unsigned char* Msg, int Size)
{
    if (Size < 5 || Msg[2] != 0xF3 || Msg[3] != 0xFE) return;

    if (Msg[4] == 0x01 && Size >= 7) {
        s_BobberX = Msg[5];
        s_BobberY = Msg[6];
        SparkleAtBobber();              // the cast lands: a small splash
    } else if (Msg[4] == 0x02) {
        SparkleAtBobber();              // the bite: splash again...
        PlayBuffer(84, 0, 0);           // ...and a sharp tug sound
    }
}
