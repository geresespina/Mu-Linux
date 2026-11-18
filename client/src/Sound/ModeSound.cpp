// ModeSound.cpp — sonido de activación de los modos. Ver ModeSound.h.
// Mismo motor que los emotes (gSound.PlayEmoteSound, miniaudio): el .wav
// suena una vez y la voz se libera sola al terminar.
#include "stdafx.h"
#include "Sound/ModeSound.h"
#include "Sound/SoundManager.h"
#include "globals.h"
#include "functions.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
// Los modos tienen que PEGAR: 0.85 para el héroe (el emote usa 0.30).
constexpr float kBaseGain = 0.85f;
constexpr float kNearDistance = 800.0f;  // 8 casillas: volumen completo
constexpr float kFarDistance  = 2500.0f; // desde aca, silencio

struct ModeDef {
    int skill;
    DWORD bit;          // bit en el bitmap de efectos físicos (entity+120)
    const char* file;
};
constexpr ModeDef kModes[] = {
    { 58, 0x400,  "ModeMangekyou.wav"  },  // Mangekyou Sharingan (Uchiha)
    { 59, 0x800,  "ModeSage.wav"       },  // Sage Mode (Senju)
    { 60, 0x1000, "ModeJinchuriki.wav" },  // Jinchuriki Mode
    { 61, 0x2000, "ModeBiju.wav"       },  // Biju Mode
};

const ModeDef* FindMode(int skill)
{
    for (const ModeDef& m : kModes)
        if (m.skill == skill) return &m;
    return nullptr;
}

// Último estado conocido por personaje. La clave del slot (entity+476)
// acompaña al puntero: si el slot se recicla para otro personaje, el
// estado viejo no vale.
struct Known {
    unsigned char* character;
    WORD key;
    DWORD modes;   // bits de kModes ya encendidos
};
std::vector<Known> s_known;

Known* FindKnown(unsigned char* character, bool create)
{
    const WORD key = *(WORD*)(character + EntityOffset(476));
    for (Known& k : s_known) {
        if (k.character != character) continue;
        if (k.key != key) { k.key = key; k.modes = 0; }  // slot reciclado
        return &k;
    }
    if (!create) return nullptr;
    if (s_known.size() >= 128) {
        // Poda: slots inactivos primero; si no hay, el más viejo.
        size_t drop = 0;
        for (size_t i = 0; i < s_known.size(); ++i)
            if (!s_known[i].character[0]) { drop = i; break; }
        s_known.erase(s_known.begin() + drop);
    }
    s_known.push_back({character, key, 0});
    return &s_known.back();
}

float DistanceGain(unsigned char* character)
{
    unsigned char* hero = (unsigned char*)(uintptr_t)DAT_07abf5d8;
    if (!hero || hero == character) return 1.0f;
    const float dx = AsObject(character)->Position[0] - AsObject(hero)->Position[0];
    const float dy = AsObject(character)->Position[1] - AsObject(hero)->Position[1];
    const float d = sqrtf(dx * dx + dy * dy);
    if (d <= kNearDistance) return 1.0f;
    if (d >= kFarDistance) return 0.0f;
    return 1.0f - (d - kNearDistance) / (kFarDistance - kNearDistance);
}
}

void ModeSound_OnModeOn(unsigned char* character, int skill)
{
    if (!character || !character[0]) return;
    const ModeDef* mode = FindMode(skill);
    if (!mode) return;
    Known* known = FindKnown(character, true);
    if (known->modes & mode->bit) return;   // ya estaba encendido: no es flanco
    known->modes |= mode->bit;

    const float gain = kBaseGain * DistanceGain(character);
    if (gain <= 0.0f) return;
    char path[300];
    snprintf(path, sizeof(path), "Data\\Sound\\Modes\\%s", mode->file);
    gSound.PlayEmoteSound(path, gain, false);
}

void ModeSound_OnModeOff(unsigned char* character, int skill)
{
    if (!character || !character[0]) return;
    const ModeDef* mode = FindMode(skill);
    if (!mode) return;
    if (Known* known = FindKnown(character, false))
        known->modes &= ~mode->bit;
}
