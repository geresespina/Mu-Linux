#include "stdafx.h"
#include "Entity/CharacterEffects.h"
#include "Render/BijuTransform.h"
#include "Entity/EntityName.h"
#include "Render/EquipmentVisuals.h"
#include "Item/ContentCatalog.h"
#include "Config/UserSettings.h"
#include "Game/MapManager.h"

extern "C" const char* Guild_GetMarkName(int row);

#define local_60 (scratch.light[0])
#define local_5c (scratch.light[1])
#define local_58 (scratch.light[2])
#define local_48 (scratch.offset[0])
#define local_44 (scratch.offset[1])
#define local_40 (scratch.offset[2])
#define local_54 (scratch.position[0])
#define local_50 (scratch.position[1])
#define local_4c (scratch.position[2])
#define local_30 (scratch.previous[0])
#define local_2c (scratch.previous[1])
#define local_28 (scratch.previous[2])
#define local_3c (scratch.extra[0])
#define local_38 (scratch.extra[1])
#define local_34 (scratch.extra[2])

// IDA: emisores iniciales de RenderCharacter (0x00456770).
// DESVIACION: azar y emisiones persistentes se evalúan sólo en el paso SIM.
void CharacterEffects_TickEarly(intptr_t entity, CharacterEffectScratch &scratch) {
    int *param_1 = (int *)entity, *puVar13 = (int *)entity;
    short sVar2 = AsObject(entity)->Type;
    int entity_type = sVar2;
    BYTE bVar7 = AsCharacter(entity)->MonsterIndex;
    void *model = (void *)(DAT_05828d58 + entity_type * BMD_STRIDE);
    const CatalogMonster *monster = entity_type != 390 ? gContentCatalog.GetMonster(bVar7) : nullptr;
    if (monster && monster->Golden) return;
    if (bVar7 == 0x45) {
        // 9-bone glitter + 3 random-bone sparks
        const float sparkle = (float)(rand() % 30 + 70) * 0.01f;
        local_60 = sparkle * 0.8f;
        local_5c = sparkle * 0.9f;
        local_58 = sparkle;
        BYTE *boneIdxTable = (BYTE *)&DAT_0055984c;
        for (int i = 0; i < 9; i++) {
            BMD_TransformPosition(model,
                (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + (int)(UINT)boneIdxTable[i] * 0x30),
                &local_48, &local_54, '\x01');
            Equipment_AddSprite(0x47e, &local_54, 0.6f, &local_60, (intptr_t)puVar13, 0.0f, 0);
        }
        // IDA changes only the random-particle tint after the nine sprites.
        local_60 = sparkle * 0.6f;
        local_5c = sparkle * 0.7f;
        local_58 = sparkle * 0.8f;
        int nBones = *(short *)((char *)model + 0x22);
        for (int i = 0; i < 3; i++) {
            // IDA: v242[0..2] = rand() % 20 - 10 antes de cada particula.
            // Sin esto las tres chispas nacen clavadas en el hueso.
            local_48 = (float)(rand() % 20 - 10);
            local_44 = (float)(rand() % 20 - 10);
            local_40 = (float)(rand() % 20 - 10);
            int r = rand();
            BMD_TransformPosition(model,
                (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + (r % nBones) * 0x30),
                &local_48, &local_54, '\x01');
            Particle_Spawn(0x498, &local_54, (float *)(puVar13 + 7),
                         &local_60, 3, 1.0f, 0);
        }
    }
    else if (bVar7 == 0x46) {
        BMD_TransformPosition(model, (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0x3c0),
                     &local_48, &local_54, '\x01');
        Equipment_AddSprite(0x47e, &local_54, 0.8f, &local_60, (intptr_t)puVar13, 0.0f, 0);
    }
    else if (bVar7 == 0x49) {
        // Drakan (MonsterID 73). IDA 00456770 `case 'I'` con 747 == 73.
        // Sprites 1150 sobre dos tramos de huesos, mas una cadena de joints
        // 1254 entre huesos consecutivos.
        //
        // IDA fija el color ANTES de la cadena: Light = (0.1, 0.1, 1.0), o sea
        // AZUL. El port no lo hacia y heredaba el (1,1,1) que queda seteado
        // antes del switch (raw L290-292), asi que la cadena salia blanca.
        local_60 = 0.1f;   // Light[0]
        local_5c = 0.1f;   // Light[1]
        local_58 = 1.0f;   // Light[2]
        for (int off = 0x270; off < 0x510; off += 0x30) {
            BMD_TransformPosition(model, (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + off),
                         &local_48, &local_54, '\x01');
            Equipment_AddSprite(0x47e, &local_54, 0.8f, &local_60, (intptr_t)puVar13, 0.0f, 0);
            // IDA: `if (v29 >= 672 && v29 <= 768 || v29 == 1104)`.
            // Al port le faltaba el `|| off == 0x450` (hueso 23), o sea un
            // tramo de la cadena no se dibujaba.
            if ((off >= 0x2a0 && off <= 0x300) || off == 0x450) {
                Joint_Create(0x4e6, &local_30, &local_54,
                             (float *)(puVar13 + 7), 7, 0, 20.0f, -1, 0);
            }
            local_30 = local_54; local_2c = local_50; local_28 = local_4c;
        }
        for (int off = 0x9c0; off < 0xb10; off += 0x30) {
            BMD_TransformPosition(model, (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + off),
                         &local_48, &local_54, '\x01');
            Equipment_AddSprite(0x47e, &local_54, 0.8f, &local_60, (intptr_t)puVar13, 0.0f, 0);
        }
    }
    else if (bVar7 == 0x4b) {
        BMD_TransformPosition(model, (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0x360),
                     &local_48, &local_54, '\x01');
        Particle_Spawn(0x4ab, &local_54, (float *)(puVar13 + 7),
                     &local_60, 0, 0.3f, 0);
    }
}

namespace {
// Aura de clan (DESVIACION, solo cliente): indice de clan segun el nombre
// de guild visible en la entidad.  Pares de color y periodo identicos a
// los del glow de item en CContentCatalog::GetItemGlow
// (Item/ContentCatalog.cpp) para que equipo y aura pulsen en fase.
int ClanAuraIndex(const char* guildName)
{
    if (guildName == nullptr) return -1;
    if (strcmp(guildName, "Senju") == 0) return 0;
    if (strcmp(guildName, "Uchiha") == 0) return 1;
    return -1;
}

void ClanAuraColor(int clan, float light[3], float intensity)
{
    static const float kClanBase[2][3] = {
        { 120.0f / 255.0f, 220.0f / 255.0f,  60.0f / 255.0f },  // Senju: verde
        { 220.0f / 255.0f,  20.0f / 255.0f,  40.0f / 255.0f },  // Uchiha: rojo
    };
    static const float kClanPair[2][3] = {
        { 1.0f,            190.0f / 255.0f,  40.0f / 255.0f },  // Senju -> dorado
        { 110.0f / 255.0f,  30.0f / 255.0f, 160.0f / 255.0f },  // Uchiha -> violeta
    };
    const float t = 0.5f - 0.5f * cosf(WorldTime * (2.0f * 3.14159265f / 2400.0f));
    for (int i = 0; i < 3; ++i)
        light[i] = (kClanBase[clan][i] + (kClanPair[clan][i] - kClanBase[clan][i]) * t) * intensity;
}
}  // namespace

// IDA: switch final por modelo de RenderCharacter (0x00456770).
void *CharacterEffects_TickLate(intptr_t entity, CharacterEffectScratch &scratch) {
    int *param_1 = (int *)entity, *puVar13 = (int *)entity;
    short sVar2 = AsObject(entity)->Type;
    int entity_type = sVar2;
    BYTE bVar7 = AsCharacter(entity)->MonsterIndex;
    void *model = (void *)(DAT_05828d58 + entity_type * BMD_STRIDE);
    void *pvVar23 = model;
    RequestTerrainLight(AsObject(entity)->Position[0], AsObject(entity)->Position[1], scratch.light);
    // ── 8. Death / PvP color tint ────────────────────────────────────────────
    // [hash-table obfuscation blocks skipped — anti-tamper, not game logic]
    if ((*(BYTE *)((char *)DAT_07cf1ff4 + 0x28) & 1) != 0) {
        local_44 = 0.9f; local_40 = 0.5f; local_3c = 0.5f;
    }
    if ((*(BYTE *)((char *)DAT_07cf1ff4 + 0x28) & 2) != 0) {
        local_44 *= _DAT_00552504;
        local_40 *= _DAT_005526e8;
        local_3c *= _DAT_00552504;
    }

    // ── 9. Entity-type outer switch — NPC / monster special effects ──────────
    // Se conserva el modelo de la pose SIM para los emisores del cuerpo.
    pvVar23 = model;

    switch (sVar2) {

    case 0x10e:  // Boat-like entity type 270
    case 300:
        // If weapon anim state == 1 (armed standing) or type == 300: sail effect
        if (((sVar2 == 0x10e) && (*(short *)&((CHARACTER*)(uintptr_t)(param_1))->Level == 1)) ||
            (sVar2 == 300)) {
            puVar13 = (int *)Entity_SpawnBoneRangeEffect((intptr_t)puVar13, 0x16, 0x17, 1.0f);
            return puVar13;
        }
        break;

    case 0x12a:  // Entity 298 — multi-bone particle rain (two tints based on flag)
    {
        int iVar9  = 0;
        int iVar18 = 0;
        local_48 = 0.0f; local_44 = 0.0f; local_40 = 0.0f;
        bool bAlt = (*(short *)&((CHARACTER*)(uintptr_t)(param_1))->Level != 0);
        if (bAlt) {
            local_60 = 0.2f; local_5c = 0.7f; local_58 = 0.1f;
        } else {
            local_60 = 1.0f; local_5c = 1.0f; local_58 = 1.0f;
        }
        int nBones = *(short *)((char *)pvVar23 + 0x22);
        while (iVar18 < nBones) {
            // Skip bones in exclusion ranges (model-specific blank bones)
            if ((*(char *)((char*)AsModel(pvVar23)->Bones + iVar9 / 0x8c * sizeof(ModelAnimationBone) + 0x22) == '\0') &&
                ((iVar9 < 0x834 || iVar9 > 0xaf0)) &&
                ((iVar9 < 0xec4 || iVar9 > 0x1180))) {
                BMD_TransformPosition(pvVar23, (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + iVar18 * 0x30),
                             &local_48, &local_54, '\x01');
                UINT uType = bAlt ? 0x4f0 : 0x4cf;
                float fSc  = bAlt ? 1.3f : 2.5f;
                Equipment_AddSprite((int)uType, &local_54, fSc, &local_60, (intptr_t)puVar13, 0, (int)!bAlt);
                // Random 1/4 chance: add spark if walking
                BYTE bAnimState = *(BYTE *)&((OBJECT*)(uintptr_t)(puVar13))->CurrentAction;
                if ((rand() & 3) == 0 && bAnimState >= 3 && bAnimState <= 4)
                    Particle_Spawn(0x49c, &local_54, (float *)(puVar13 + 7),
                                 &local_60, 0, 1.0f, 0);
            }
            iVar9  += 0x8c;
            iVar18 += 1;
        }
        break;
    }

    case 0x12f:  // Entity 303 — dual smoke/fire column
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x497, 4.0f, 9, 0.0f, 0, 5.0f);
        puVar13 = (int *)Entity_SpawnBoneEffect((intptr_t)puVar13, 0x4d0, 3.0f, 9, 0.0f, 0, 5.0f);
        return puVar13;

    case 0x130:  // Entity 304 — six-point smoke/fire/magic
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x4a7, 0.5f, 0x1e, 0.0f, 0, -5.0f);
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x4a7, 0.5f, 0x27, 0.0f, 0, -5.0f);
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x497, 4.0f, 0x1e, 0.0f, 0, -5.0f);
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x497, 4.0f, 0x27, 0.0f, 0, -5.0f);
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x4d0, 2.0f, 0x1e, 0.0f, 0, -5.0f);
        puVar13 = (int *)Entity_SpawnBoneEffect((intptr_t)puVar13, 0x4d0, 2.0f, 0x27, 0.0f, 0, -5.0f);
        return puVar13;

    case 0x132:  // Entity 306 — claw/spider: beam + 8 effect bones
        Entity_SpawnBoneRangeEffect((intptr_t)puVar13, 0x2a, 0x2b, 1.0f);
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x497, 2.0f, 0x1a, 0.0f, 0, 0.0f);
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x497, 2.0f, 0x1f, 0.0f, 0, 0.0f);
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x497, 2.0f, 0x24, 0.0f, 0, 0.0f);
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x497, 2.0f, 0x29, 0.0f, 0, 0.0f);
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x4d0, 1.0f, 0x1a, 0.0f, 0, 0.0f);
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x4d0, 1.0f, 0x1f, 0.0f, 0, 0.0f);
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x4d0, 1.0f, 0x24, 0.0f, 0, 0.0f);
        puVar13 = (int *)Entity_SpawnBoneEffect((intptr_t)puVar13, 0x4d0, 1.0f, 0x29, 0.0f, 0, 0.0f);
        return puVar13;

    case 0x133:  // Entity 307 — large fire/smoke at bone 0x3f
        Entity_SpawnBoneEffect((intptr_t)puVar13, 0x4a7, 1.0f, 0x3f, 0.0f, 0, 20.0f);
        puVar13 = (int *)Entity_SpawnBoneEffect((intptr_t)puVar13, 0x4d0, 4.0f, 0x3f, 0.0f, 0, 20.0f);
        return puVar13;

    case 0x142:  // Entity 322 — dual beam bones 0x1a/0x1b  scale=2
        puVar13 = (int *)Entity_SpawnBoneRangeEffect((intptr_t)puVar13, 0x1a, 0x1b, 2.0f);
        return puVar13;

    case 0x15c:  // Entity 348 — soft sparkle at bone 0x20
        puVar13 = (int *)Entity_SpawnBoneEffect((intptr_t)puVar13, 0x47e, 1.5f, 0x20, 0.0f, 0, 0.0f);
        return puVar13;

    case 0x15d:  // Entity 349 — sin-wave orb cloud + occasional lightning joint
    {
        float fSin = (float)(double)fsin((double)(WorldTime * _DAT_005528e0));
        local_48 = 3.5f; local_44 = -12.0f; local_40 = 10.0f;
        float fCol = fSin * _DAT_0055294c + _DAT_00552948;
        local_60 = fCol; local_5c = fCol; local_58 = fCol;
        BMD_TransformPosition(pvVar23, (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0x3c0),
                     &local_48, &local_54, '\x01');
        Equipment_AddSprite(0x4a7, &local_54, 0.3f, &local_60, (intptr_t)puVar13,
                     (float)WorldTime * _DAT_00552944, 0);
        Equipment_AddSprite(0x4a7, &local_54, 0.3f, &local_60, (intptr_t)puVar13,
                     -(float)WorldTime * _DAT_00552944, 0);
        if (rand() % 0x1e == 0) {
            // Scatter lightning from random offsets around bone position
            local_48 = (float)(rand() % 100) + local_54 - _DAT_00552598;
            local_44 = (float)(rand() % 100) + local_50 - _DAT_00552598;
            local_40 = (float)(rand() % 100) + local_4c - _DAT_00552598;
            puVar13 = (int *)Joint_Create(0x4ea, &local_48, &local_54,
                                          (float *)(puVar13 + 7), 6, 0, 20.0f, -1, 0);
            return puVar13;
        }
        break;
    }

    case 0x176:  // Entity 374 — sparkle at bone 6  scale=2
        puVar13 = (int *)Entity_SpawnBoneEffect((intptr_t)puVar13, 0x47e, 2.0f, 6, 0.0f, 0, 0.0f);
        return puVar13;

    case 0x186:  // Entity 390 (Player) — port completo IDA case 390 (líneas 2111-2269)
    {
        // CheckFullSet detecta si las 5 piezas de armadura forman un set
        // matcheado con level >= 9 → setea `EquipmentLevelSet` global y devuelve
        // true si el char tiene 5 piezas (aunque no matcheen, para v230=true).
        bool v230 = CheckFullSet(reinterpret_cast<uintptr_t>(param_1));
        local_48 = 0.0f; local_44 = 0.0f; local_40 = 0.0f;

        if (SceneFlag == 2) {
            return puVar13;
        }

        // ── 0. Aura de clan: pulso bicolor alrededor del personaje ────
        // DESVIACION (solo cliente): un miembro de Senju/Uchiha irradia
        // el glow de su clan lleve equipo o no — el glow es del clan, no
        // del item, asi que NO depende del set completo (paso 4).  Vale
        // para el heroe y para cualquier jugador en pantalla: el nombre
        // de guild llega en la marca (GuildMarkIndex) de cada entidad.
        // Dos sprites 1150 (el glow suave del juego) por frame: uno en
        // los pies (luz del suelo) y uno en el hueso raiz (halo del
        // cuerpo), intensidad modesta para que se lea como aura.
        if (bVar7 == 0xFF && AsCharacter(puVar13)->Dead == 0 &&
            !gUserSettings.GetAntilag(ANTILAG_GLOW)) {
            const short guildIdx = AsCharacter(puVar13)->GuildMarkIndex;
            const int clan = (guildIdx != -1)
                ? ClanAuraIndex(Guild_GetMarkName(guildIdx)) : -1;
            if (clan >= 0) {
                float auraLight[3];
                float auraPos[3] = { AsObject(puVar13)->Position[0],
                                     AsObject(puVar13)->Position[1],
                                     AsObject(puVar13)->Position[2] };
                ClanAuraColor(clan, auraLight, 0.5f);
                Equipment_AddSprite(0x47e, auraPos, 2.6f, auraLight,
                                    (intptr_t)puVar13, 0.0f, 0);
                float auraOff[3] = { 0.0f, 0.0f, 0.0f };
                float auraBody[3];
                BMD_TransformPosition(pvVar23,
                    (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0 * 0x30),
                    auraOff, auraBody, '\x01');
                ClanAuraColor(clan, auraLight, 0.35f);
                Equipment_AddSprite(0x47e, auraBody, 1.8f, auraLight,
                                    (intptr_t)puVar13, 0.0f, 0);
            }
        }

        // ── 0b. Aura Mangekyou (DESVIACION, solo cliente; MEAMU
        // 2026-11-15) ────────────────────────────────────────────
        // El buff Mangekyou Sharingan (efecto GS 65) llega como el
        // bit 0x0400 del estado persistente (entity+120): viewport
        // F3:F1/F3:F2 al entrar en pantalla y C1:07 al castear, asi
        // que el aura la ve todo el que ve al personaje, durante
        // exactamente la vida del buff. Mismas herramientas que el
        // aura de clan pero MAS GRANDE y en carmesi puro: sprite de
        // suelo 8.4 (clan: 2.6), halo corporal 5.5 (clan: 1.8),
        // pulso de 1200 ms (clan: 2400 ms) y deriva de brasas
        // (particula 0x4ab) con compuerta temporal por entidad.
        // MEAMU 2026-11-16 (pedido de G: el aura MAS GRANDE): suelo
        // 4.2 -> 8.4 y halo 3.0 -> 5.5; las brasas nacen repartidas
        // en un anillo alrededor del cuerpo de radio halo x 18 (la
        // escala de la 0x4ab la fija su propio spawn, no el caller),
        // con angulo determinista por tiempo+entidad (sin azar).
        if (bVar7 == 0xFF && AsCharacter(puVar13)->Dead == 0 &&
            !gUserSettings.GetAntilag(ANTILAG_GLOW) &&
            (*(DWORD*)((BYTE*)puVar13 + 120) & 0x0400) != 0) {
            const float mt = 0.5f - 0.5f * cosf(WorldTime * (2.0f * 3.14159265f / 1200.0f));
            float mkLight[3] = { 0.86f + (1.0f - 0.86f) * mt,
                                 0.08f + (0.25f - 0.08f) * mt,
                                 0.16f + (0.20f - 0.16f) * mt };
            float mkPos[3] = { AsObject(puVar13)->Position[0],
                               AsObject(puVar13)->Position[1],
                               AsObject(puVar13)->Position[2] };
            float mkGround[3] = { mkLight[0] * 0.6f, mkLight[1] * 0.6f, mkLight[2] * 0.6f };
            Equipment_AddSprite(0x47e, mkPos, 8.4f, mkGround,
                                (intptr_t)puVar13, 0.0f, 0);
            float mkOff[3] = { 0.0f, 0.0f, 0.0f };
            float mkBody[3];
            BMD_TransformPosition(pvVar23,
                (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0 * 0x30),
                mkOff, mkBody, '\x01');
            Equipment_AddSprite(0x47e, mkBody, 5.5f, mkLight,
                                (intptr_t)puVar13, 0.0f, 0);
            const int mkPhase = (int)(((intptr_t)puVar13 >> 5) & 0x3F);
            if ((((int)WorldTime + mkPhase) % 120) < 16) {
                float emberLight[3] = { 1.0f, 0.3f, 0.15f };
                const float mkAng = (WorldTime + (float)mkPhase * 61.0f) *
                                    (2.0f * 3.14159265f / 2400.0f);
                const float mkRad = 5.5f * 18.0f;
                float emberPos[3] = { mkBody[0] + cosf(mkAng) * mkRad,
                                      mkBody[1] + sinf(mkAng) * mkRad,
                                      mkBody[2] };
                Particle_Spawn(0x4ab, emberPos, (float *)(puVar13 + 7),
                               emberLight, 0, 0.45f, 0);
            }
        }

        // ── 0c. Aura Sage Mode (DESVIACION, solo cliente; MEAMU
        // 2026-11-17) ────────────────────────────────────────────
        // El espejo Senju del aura Mangekyou (0b): el buff Sage
        // Mode (efecto GS 66) llega como el bit 0x0800 del estado
        // persistente (entity+120). Mismo tamano GRANDE que el
        // carmesi (suelo 8.4, halo 5.5, pulso 1200 ms, motas en
        // anillo con la particula 0x4ab) pero en verde naturaleza
        // Senju (#78dc3c) que respira hacia el dorado (#d8a93b):
        // energia natural, inconfundible frente al carmesi a
        // simple vista.
        if (bVar7 == 0xFF && AsCharacter(puVar13)->Dead == 0 &&
            !gUserSettings.GetAntilag(ANTILAG_GLOW) &&
            (*(DWORD*)((BYTE*)puVar13 + 120) & 0x0800) != 0) {
            const float st = 0.5f - 0.5f * cosf(WorldTime * (2.0f * 3.14159265f / 1200.0f));
            float sgLight[3] = { 0.47f + (0.85f - 0.47f) * st,
                                 0.86f + (0.66f - 0.86f) * st,
                                 0.24f + (0.23f - 0.24f) * st };
            float sgPos[3] = { AsObject(puVar13)->Position[0],
                               AsObject(puVar13)->Position[1],
                               AsObject(puVar13)->Position[2] };
            float sgGround[3] = { sgLight[0] * 0.6f, sgLight[1] * 0.6f, sgLight[2] * 0.6f };
            Equipment_AddSprite(0x47e, sgPos, 8.4f, sgGround,
                                (intptr_t)puVar13, 0.0f, 0);
            float sgOff[3] = { 0.0f, 0.0f, 0.0f };
            float sgBody[3];
            BMD_TransformPosition(pvVar23,
                (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0 * 0x30),
                sgOff, sgBody, '\x01');
            Equipment_AddSprite(0x47e, sgBody, 5.5f, sgLight,
                                (intptr_t)puVar13, 0.0f, 0);
            const int sgPhase = (int)(((intptr_t)puVar13 >> 5) & 0x3F);
            if ((((int)WorldTime + sgPhase) % 120) < 16) {
                float moteLight[3] = { 0.6f, 1.0f, 0.4f };
                const float sgAng = (WorldTime + (float)sgPhase * 61.0f) *
                                    (2.0f * 3.14159265f / 2400.0f);
                const float sgRad = 5.5f * 18.0f;
                float motePos[3] = { sgBody[0] + cosf(sgAng) * sgRad,
                                     sgBody[1] + sinf(sgAng) * sgRad,
                                     sgBody[2] };
                Particle_Spawn(0x4ab, motePos, (float *)(puVar13 + 7),
                               moteLight, 0, 0.45f, 0);
            }
        }

        // ── 0d. Aura Jinchuriki Mode (DESVIACION, solo cliente;
        // MEAMU 2026-11-17, Train 4) ─────────────────────────────
        // G: "BIG BIG BIG AURA and GLOW", coloured by the equipped
        // beast. El buff Jinchuriki Mode (skill 60) llega como el
        // bit 0x1000 del estado persistente (entity+120), el mismo
        // canal que 0x0400/0x0800, asi que el aura la ve todo el que
        // ve al personaje. El tinte sale de la tabla lateral de
        // BijuTransform (BeastFor/JinchurikiTint): el pet equipado
        // (seccion 13, subs 496-505) lo alimentan TODOS los caminos
        // de viewport (heroe y remotos, Recv_Viewport incluido);
        // sin pet conocido cae al dorado Kurama. Escala = 3x el
        // aura Mangekyou (suelo 8.4 -> 25.2, halo 5.5 -> 16.5) mas
        // dos capas de GLOW: un halo exterior enorme y tenue
        // (34.0, tinte x0.28) y un nucleo corporal brillante
        // (8.4, tinte aclarado hacia blanco). Pulso 1200 ms y
        // brasas 0x4ab en anillo, tintadas como la bestia.
        if (bVar7 == 0xFF && AsCharacter(puVar13)->Dead == 0 &&
            !gUserSettings.GetAntilag(ANTILAG_GLOW) &&
            ((*(DWORD*)((BYTE*)puVar13 + 120) & 0x1000) != 0 ||
             BijuTransform::JinchurikiVisualOn((const CHARACTER*)puVar13))) {   // TRAIN 125: bit o latch
            float jkBase[3] = { 0.961f, 0.522f, 0.122f };  // Kurama
            BijuTransform::JinchurikiTint((const CHARACTER*)puVar13, jkBase);
            const float jt = 0.5f - 0.5f * cosf(WorldTime * (2.0f * 3.14159265f / 1200.0f));
            const float jkPulse = 0.82f + 0.30f * jt;
            float jkLight[3] = { jkBase[0] * jkPulse,
                                 jkBase[1] * jkPulse,
                                 jkBase[2] * jkPulse };
            float jkPos[3] = { AsObject(puVar13)->Position[0],
                               AsObject(puVar13)->Position[1],
                               AsObject(puVar13)->Position[2] };
            float jkHalo[3] = { jkBase[0] * 0.28f, jkBase[1] * 0.28f, jkBase[2] * 0.28f };
            Equipment_AddSprite(0x47e, jkPos, 34.0f, jkHalo,
                                (intptr_t)puVar13, 0.0f, 0);
            float jkGround[3] = { jkLight[0] * 0.6f, jkLight[1] * 0.6f, jkLight[2] * 0.6f };
            Equipment_AddSprite(0x47e, jkPos, 25.2f, jkGround,
                                (intptr_t)puVar13, 0.0f, 0);
            float jkOff[3] = { 0.0f, 0.0f, 0.0f };
            float jkBody[3];
            BMD_TransformPosition(pvVar23,
                (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0 * 0x30),
                jkOff, jkBody, '\x01');
            Equipment_AddSprite(0x47e, jkBody, 16.5f, jkLight,
                                (intptr_t)puVar13, 0.0f, 0);
            float jkCore[3] = { jkBase[0] + (1.0f - jkBase[0]) * 0.35f,
                                jkBase[1] + (1.0f - jkBase[1]) * 0.35f,
                                jkBase[2] + (1.0f - jkBase[2]) * 0.35f };
            Equipment_AddSprite(0x47e, jkBody, 8.4f, jkCore,
                                (intptr_t)puVar13, 0.0f, 0);
            const int jkPhase = (int)(((intptr_t)puVar13 >> 5) & 0x3F);
            if ((((int)WorldTime + jkPhase) % 120) < 16) {
                float jkEmber[3] = { jkBase[0], jkBase[1], jkBase[2] };
                const float jkAng = (WorldTime + (float)jkPhase * 61.0f) *
                                    (2.0f * 3.14159265f / 2400.0f);
                const float jkRad = 16.5f * 18.0f;
                float emberPos[3] = { jkBody[0] + cosf(jkAng) * jkRad,
                                      jkBody[1] + sinf(jkAng) * jkRad,
                                      jkBody[2] };
                Particle_Spawn(0x4ab, emberPos, (float *)(puVar13 + 7),
                               jkEmber, 0, 0.6f, 0);
            }
        }

        // ── 0e. Aura Chakra Cloak (DESVIACION, solo cliente; MEAMU
        // 2026-11-18, Train 6) ───────────────────────────────────
        // G's live verdict on Bounce 5/7: skill 73 worked (x5) but
        // had NO in-world visual -- Modes-2 shipped only the status
        // icon (Train 3, bitmap 0x0c8) and the Skills.cpp 0x49
        // comment promised an aura this block never had. The buff
        // arrives as bit 0x4000 of the persistent state (entity+120),
        // the same channel as 0x0400/0x0800/0x1000, so the shroud is
        // visible to everyone in view for exactly the buff's life.
        // Fiery ORANGE-GOLD Kurama-cloak energy: between Sage (suelo
        // 8.4 / halo 5.5) and Jinchuriki (25.2 / 16.5) in size, with
        // a white-hot core and a FASTER pulse (900 ms -- flame
        // flicker, not the 1200 ms breathing of the other modes),
        // so it never reads as the beast-tinted Jinchuriki aura
        // (which is also bigger) nor as Sage green. Brasas 0x4ab en
        // anillo, naranjas.
        if (bVar7 == 0xFF && AsCharacter(puVar13)->Dead == 0 &&
            !gUserSettings.GetAntilag(ANTILAG_GLOW) &&
            (*(DWORD*)((BYTE*)puVar13 + 120) & 0x4000) != 0) {
            const float ct = 0.5f - 0.5f * cosf(WorldTime * (2.0f * 3.14159265f / 900.0f));
            float ckLight[3] = { 1.0f,
                                 0.42f + (0.68f - 0.42f) * ct,
                                 0.06f + (0.22f - 0.06f) * ct };
            float ckPos[3] = { AsObject(puVar13)->Position[0],
                               AsObject(puVar13)->Position[1],
                               AsObject(puVar13)->Position[2] };
            float ckGround[3] = { ckLight[0] * 0.6f, ckLight[1] * 0.6f, ckLight[2] * 0.6f };
            Equipment_AddSprite(0x47e, ckPos, 12.6f, ckGround,
                                (intptr_t)puVar13, 0.0f, 0);
            float ckOff[3] = { 0.0f, 0.0f, 0.0f };
            float ckBody[3];
            BMD_TransformPosition(pvVar23,
                (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0 * 0x30),
                ckOff, ckBody, '\x01');
            Equipment_AddSprite(0x47e, ckBody, 9.0f, ckLight,
                                (intptr_t)puVar13, 0.0f, 0);
            float ckCore[3] = { ckLight[0] + (1.0f - ckLight[0]) * 0.45f,
                                ckLight[1] + (1.0f - ckLight[1]) * 0.45f,
                                ckLight[2] + (1.0f - ckLight[2]) * 0.45f };
            Equipment_AddSprite(0x47e, ckBody, 5.0f, ckCore,
                                (intptr_t)puVar13, 0.0f, 0);
            const int ckPhase = (int)(((intptr_t)puVar13 >> 5) & 0x3F);
            if ((((int)WorldTime + ckPhase) % 120) < 16) {
                float ckEmber[3] = { 1.0f, 0.5f, 0.12f };
                const float ckAng = (WorldTime + (float)ckPhase * 61.0f) *
                                    (2.0f * 3.14159265f / 2400.0f);
                const float ckRad = 9.0f * 18.0f;
                float emberPos[3] = { ckBody[0] + cosf(ckAng) * ckRad,
                                      ckBody[1] + sinf(ckAng) * ckRad,
                                      ckBody[2] };
                Particle_Spawn(0x4ab, emberPos, (float *)(puVar13 + 7),
                               ckEmber, 0, 0.5f, 0);
            }
        }

        // ── 0f. Aura Dragon Sage Mode (DESVIACION, solo cliente;
        // MEAMU 2026-11-18, Train 6) ─────────────────────────────
        // Skill 75 is built on the Sage chassis server-side but
        // carries its OWN state bit (0x8000), so the Sage block
        // (0c, bit 0x0800) never fires for it and the mode rendered
        // with no aura at all. This is the Sage aura verbatim --
        // same size, same Senju green breathing to gold, same motes
        // -- keyed on 0x8000: Dragon Sage IS Sage-family energy, so
        // sharing the family's look is the design. The modes are
        // mutually exclusive server-side, so 0c and 0f never draw
        // on the same character at once.
        if (bVar7 == 0xFF && AsCharacter(puVar13)->Dead == 0 &&
            !gUserSettings.GetAntilag(ANTILAG_GLOW) &&
            (*(DWORD*)((BYTE*)puVar13 + 120) & 0x8000) != 0) {
            const float dt = 0.5f - 0.5f * cosf(WorldTime * (2.0f * 3.14159265f / 1200.0f));
            float dsLight[3] = { 0.47f + (0.85f - 0.47f) * dt,
                                 0.86f + (0.66f - 0.86f) * dt,
                                 0.24f + (0.23f - 0.24f) * dt };
            float dsPos[3] = { AsObject(puVar13)->Position[0],
                               AsObject(puVar13)->Position[1],
                               AsObject(puVar13)->Position[2] };
            float dsGround[3] = { dsLight[0] * 0.6f, dsLight[1] * 0.6f, dsLight[2] * 0.6f };
            Equipment_AddSprite(0x47e, dsPos, 8.4f, dsGround,
                                (intptr_t)puVar13, 0.0f, 0);
            float dsOff[3] = { 0.0f, 0.0f, 0.0f };
            float dsBody[3];
            BMD_TransformPosition(pvVar23,
                (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0 * 0x30),
                dsOff, dsBody, '\x01');
            Equipment_AddSprite(0x47e, dsBody, 5.5f, dsLight,
                                (intptr_t)puVar13, 0.0f, 0);
            const int dsPhase = (int)(((intptr_t)puVar13 >> 5) & 0x3F);
            if ((((int)WorldTime + dsPhase) % 120) < 16) {
                float moteLight[3] = { 0.6f, 1.0f, 0.4f };
                const float dsAng = (WorldTime + (float)dsPhase * 61.0f) *
                                    (2.0f * 3.14159265f / 2400.0f);
                const float dsRad = 5.5f * 18.0f;
                float motePos[3] = { dsBody[0] + cosf(dsAng) * dsRad,
                                     dsBody[1] + sinf(dsAng) * dsRad,
                                     dsBody[2] };
                Particle_Spawn(0x4ab, motePos, (float *)(puVar13 + 7),
                               moteLight, 0, 0.45f, 0);
            }
        }


        // MEAMU TRAIN 8 (2026-11-19): GIANT FORM AURA -- G's live
        // verdict on Train 7: "i need a wayyyyy bigger glow. i cant
        // see it at all. BIG HUGE GIGANTIC GLOW please". The form
        // aura (0g Susanoo 228 + 0h Karane 229) outer halo goes from
        // 46 to kFormAuraHalo world units (~x3; target read: glow
        // diameter ~4-5x the character's height on screen). ONE
        // constant drives BOTH forms; inner layers keep their
        // Train 7 proportions to the halo, and the halo/ground
        // alpha factors drop (0.26->0.16, 0.6->0.45) so the bigger
        // disc stays a soft aura, not a solid disc. The 1500 ms
        // breath cycle is unchanged; embers keep their orbit and
        // cadence, size 0.6->0.9. Future tune = this one number.
        static constexpr float kFormAuraHalo = 140.0f;

        // ── 0g. Aura Perfect Susanoo (DESVIACION, solo cliente;
        // MEAMU 2026-11-18, Train 7) ─────────────────────────────
        // G's order, verbatim: "give it a HUUUUUUUGE purple glow".
        // Perfect Susanoo (GS skill 74, effect 71) rides the SHARED
        // Biju form bit 0x2000, so a bit test alone would set every
        // Biju Mode beast ablaze too. The gate therefore replays
        // the render path's own decision -- BijuTransform::Resolve
        // (BijuTransform.cpp) resolves the form through
        // FormClassFor(formBit, beastBySlot): the aura fires ONLY
        // when that resolution is class 228 (form bit on, NO soul
        // pet known), i.e. exactly the entities DrawBody renders
        // as the purple titan, hero and remotes alike. Biju Mode
        // (beast known -> class 155-164) never enters here, and
        // the Cloak (0e) / Dragon Sage (0f) blocks are untouched.
        // MEAMU TRAIN 8 ADDENDUM (2026-11-19): the gate now calls
        // FormClassForEntity -- the render path's own decision,
        // form-source aware: a witnessed Susanoo cast (74) resolves
        // to 228/229 even WITH a soul pet equipped (G's ruling:
        // Susanoo = always the titan), so this aura follows the
        // titan body in that case too; Biju Mode (61) still never
        // enters (its resolution is the beast class).
        // Scale: the titan stands at catalog scale 2.0 (~450 world
        // units of wingspan), so the shroud is built ~1.35x the
        // Jinchuriki aura (0d: halo 34 / suelo 25.2 / halo 16.5),
        // the biggest aura in the game: halo exterior 46.0 tenue,
        // suelo 34.0, cuerpo 23.0, nucleo 11.0 aclarado a blanco.
        // Colour: Susanoo chakra violet (0.60, 0.24, 1.00) que
        // respira a morado profundo (0.40, 0.10, 0.78), pulso
        // lento de 1500 ms -- pesado, amenazante, nunca fucsia.
        // Coste: 4 sprites aditivos 0x47e + brasas 0x4ab en anillo
        // con la misma cadencia de 120 ms que 0c-0f; el mismo coste
        // por entidad que el aura Jinchuriki.
        if (bVar7 == 0xFF && AsCharacter(puVar13)->Dead == 0 &&
            !gUserSettings.GetAntilag(ANTILAG_GLOW) &&
            (*(DWORD*)((BYTE*)puVar13 + 120) & 0x2000) != 0 &&
            BijuTransform::FormClassForEntity((const CHARACTER*)puVar13) == 228) {
            const float st7 = 0.5f - 0.5f * cosf(WorldTime * (2.0f * 3.14159265f / 1500.0f));
            float suBase[3] = { 0.40f + (0.60f - 0.40f) * st7,
                                0.10f + (0.24f - 0.10f) * st7,
                                0.78f + (1.00f - 0.78f) * st7 };
            const float suPulse = 0.85f + 0.25f * st7;
            float suLight[3] = { suBase[0] * suPulse,
                                 suBase[1] * suPulse,
                                 suBase[2] * suPulse };
            float suPos[3] = { AsObject(puVar13)->Position[0],
                               AsObject(puVar13)->Position[1],
                               AsObject(puVar13)->Position[2] };
            float suHalo[3] = { suBase[0] * 0.16f, suBase[1] * 0.16f, suBase[2] * 0.16f };
            Equipment_AddSprite(0x47e, suPos, kFormAuraHalo, suHalo,
                                (intptr_t)puVar13, 0.0f, 0);
            float suGround[3] = { suLight[0] * 0.45f, suLight[1] * 0.45f, suLight[2] * 0.45f };
            Equipment_AddSprite(0x47e, suPos, kFormAuraHalo * 0.739f, suGround,
                                (intptr_t)puVar13, 0.0f, 0);
            float suOff[3] = { 0.0f, 0.0f, 0.0f };
            float suBody[3];
            BMD_TransformPosition(pvVar23,
                (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0 * 0x30),
                suOff, suBody, '\x01');
            Equipment_AddSprite(0x47e, suBody, kFormAuraHalo * 0.5f, suLight,
                                (intptr_t)puVar13, 0.0f, 0);
            float suCore[3] = { suBase[0] + (1.0f - suBase[0]) * 0.40f,
                                suBase[1] + (1.0f - suBase[1]) * 0.40f,
                                suBase[2] + (1.0f - suBase[2]) * 0.40f };
            Equipment_AddSprite(0x47e, suBody, kFormAuraHalo * 0.239f, suCore,
                                (intptr_t)puVar13, 0.0f, 0);
            const int suPhase = (int)(((intptr_t)puVar13 >> 5) & 0x3F);
            if ((((int)WorldTime + suPhase) % 120) < 16) {
                float suEmber[3] = { suBase[0], suBase[1], suBase[2] };
                const float suAng = (WorldTime + (float)suPhase * 61.0f) *
                                    (2.0f * 3.14159265f / 2400.0f);
                const float suRad = 23.0f * 18.0f;
                float emberPos[3] = { suBody[0] + cosf(suAng) * suRad,
                                      suBody[1] + sinf(suAng) * suRad,
                                      suBody[2] };
                Particle_Spawn(0x4ab, emberPos, (float *)(puVar13 + 7),
                               suEmber, 0, 0.9f, 0);
            }
        }


        // ── 0h. Aura BLACK GOLD Karane (Train 71: crimson retired) (DESVIACION, solo cliente;
        // MEAMU 2026-11-18, Train 7) ─────────────────────────────
        // La gemela carmesi del 0g: la forma personal de G. El
        // gate reproduce la decision con nombre de Resolve --
        // FormClassForNamed == 229 solo ocurre para Gloccy /
        // M4D4R4 con el bit de forma activo y SIN pet conocido,
        // exactamente las entidades que DrawBody renderiza como
        // KaraneCrimson. Mismo stack de 4 sprites 0x47e y la misma
        // cadencia de brasas que el 0g (halo 46.0 / suelo 34.0 /
        // cuerpo 23.0 / nucleo 11.0 -- sobre la figura Karane,
        // mas pequena que el titan, el halo llena ~1.6x su altura,
        // como el mock aprobado), pero en carmesi profundo
        // (0.74, 0.08, 0.12) que respira a sangre oscura
        // (0.50, 0.04, 0.07), pulso lento de 1500 ms y nucleo
        // aclarado hacia brasa (1.00, 0.38, 0.18). El 228 sigue
        // morado; el Cloak (0e) no se toca.
        if (bVar7 == 0xFF && AsCharacter(puVar13)->Dead == 0 &&
            !gUserSettings.GetAntilag(ANTILAG_GLOW) &&
            (*(DWORD*)((BYTE*)puVar13 + 120) & 0x2000) != 0 &&
            BijuTransform::FormClassForEntity((const CHARACTER*)puVar13) == 229) {
            // MEAMU Train 71 (2026-11-20): G picked BLACK GOLD for
            // Gloccy's 229 and ordered a BIG aura/glow/effects
            // package while the form is ON. The breath now runs
            // deep antique gold (0.55, 0.38, 0.06) to bright gold
            // (0.98, 0.76, 0.22); the core heats toward amber-white
            // (1.00, 0.85, 0.45) below. Sizes: this form gets its
            // OWN halo (kKaraneGoldHalo, ~1.65x the shared form
            // halo) plus an extra ultra-soft outer glow sprite --
            // 228/230 keep the shared 140 untouched.
            static constexpr float kKaraneGoldHalo = 230.0f;
            const float ct7 = 0.5f - 0.5f * cosf(WorldTime * (2.0f * 3.14159265f / 1500.0f));
            float kcBase[3] = { 0.55f + (0.98f - 0.55f) * ct7,
                                0.38f + (0.76f - 0.38f) * ct7,
                                0.06f + (0.22f - 0.06f) * ct7 };
            const float kcPulse = 0.85f + 0.25f * ct7;
            float kcLight[3] = { kcBase[0] * kcPulse,
                                 kcBase[1] * kcPulse,
                                 kcBase[2] * kcPulse };
            float kcPos[3] = { AsObject(puVar13)->Position[0],
                               AsObject(puVar13)->Position[1],
                               AsObject(puVar13)->Position[2] };
            float kcHalo[3] = { kcBase[0] * 0.16f, kcBase[1] * 0.16f, kcBase[2] * 0.16f };
            Equipment_AddSprite(0x47e, kcPos, kKaraneGoldHalo, kcHalo,
                                (intptr_t)puVar13, 0.0f, 0);
            // Train 71: the BIG outer glow -- a second disc at
            // 1.6x the halo, barely-there alpha, so the form sits
            // inside a gold bloom you can see across the map.
            float kcOuter[3] = { kcBase[0] * 0.07f, kcBase[1] * 0.07f, kcBase[2] * 0.07f };
            Equipment_AddSprite(0x47e, kcPos, kKaraneGoldHalo * 1.6f, kcOuter,
                                (intptr_t)puVar13, 0.0f, 0);
            float kcGround[3] = { kcLight[0] * 0.45f, kcLight[1] * 0.45f, kcLight[2] * 0.45f };
            Equipment_AddSprite(0x47e, kcPos, kKaraneGoldHalo * 0.739f, kcGround,
                                (intptr_t)puVar13, 0.0f, 0);
            float kcOff[3] = { 0.0f, 0.0f, 0.0f };
            float kcBody[3];
            BMD_TransformPosition(pvVar23,
                (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0 * 0x30),
                kcOff, kcBody, '\x01');
            Equipment_AddSprite(0x47e, kcBody, kKaraneGoldHalo * 0.5f, kcLight,
                                (intptr_t)puVar13, 0.0f, 0);
            float kcCore[3] = { kcBase[0] + (1.00f - kcBase[0]) * 0.45f,
                                kcBase[1] + (0.85f - kcBase[1]) * 0.45f,
                                kcBase[2] + (0.45f - kcBase[2]) * 0.45f };
            Equipment_AddSprite(0x47e, kcBody, kKaraneGoldHalo * 0.239f, kcCore,
                                (intptr_t)puVar13, 0.0f, 0);
            const int kcPhase = (int)(((intptr_t)puVar13 >> 5) & 0x3F);
            if ((((int)WorldTime + kcPhase) % 80) < 16) {   // Train 71: denser gold embers (120 -> 80 ms cadence)
                float kcEmber[3] = { kcBase[0], kcBase[1], kcBase[2] };
                const float kcAng = (WorldTime + (float)kcPhase * 61.0f) *
                                    (2.0f * 3.14159265f / 2400.0f);
                const float kcRad = 23.0f * 18.0f;
                float emberPos[3] = { kcBody[0] + cosf(kcAng) * kcRad,
                                      kcBody[1] + sinf(kcAng) * kcRad,
                                      kcBody[2] };
                Particle_Spawn(0x4ab, emberPos, (float *)(puVar13 + 7),
                               kcEmber, 0, 1.3f, 0);   // Train 71: bigger embers (0.9 -> 1.3)
                // Train 71: a counter-rotating twin ember -- the
                // pair reads as a storm of gold sparks around him.
                float emberPos2[3] = { kcBody[0] - cosf(kcAng) * kcRad,
                                       kcBody[1] - sinf(kcAng) * kcRad,
                                       kcBody[2] };
                Particle_Spawn(0x4ab, emberPos2, (float *)(puVar13 + 7),
                               kcEmber, 0, 1.3f, 0);
            }
        }



        // ── 0i. Aura Blue Megazord (DESVIACION, solo cliente;
        // MEAMU 2026-11-20, Train 67) ──────────────────────────
        // Eliger's personal Susanoo: the BLUE MEGAZORD (class
        // 230, the HurricaneBlue skin BMD at titan scale). Twin
        // of the 0g/0h stack -- same 4 sprites 0x47e, same halo,
        // same ember cadence -- in Megazord blue: deep blue
        // (0.10, 0.30, 0.78) breathing to bright zord blue
        // (0.16, 0.48, 1.00), core lightened toward cyan-white
        // (0.55, 0.82, 1.00). Gate == 230 exactly, so it follows
        // the Megazord body and nothing else.
        if (bVar7 == 0xFF && AsCharacter(puVar13)->Dead == 0 &&
            !gUserSettings.GetAntilag(ANTILAG_GLOW) &&
            (*(DWORD*)((BYTE*)puVar13 + 120) & 0x2000) != 0 &&
            BijuTransform::FormClassForEntity((const CHARACTER*)puVar13) == 230) {
            const float mz7 = 0.5f - 0.5f * cosf(WorldTime * (2.0f * 3.14159265f / 1500.0f));
            float mzBase[3] = { 0.10f + (0.16f - 0.10f) * mz7,
                                0.30f + (0.48f - 0.30f) * mz7,
                                0.78f + (1.00f - 0.78f) * mz7 };
            const float mzPulse = 0.85f + 0.25f * mz7;
            float mzLight[3] = { mzBase[0] * mzPulse,
                                 mzBase[1] * mzPulse,
                                 mzBase[2] * mzPulse };
            float mzPos[3] = { AsObject(puVar13)->Position[0],
                               AsObject(puVar13)->Position[1],
                               AsObject(puVar13)->Position[2] };
            float mzHalo[3] = { mzBase[0] * 0.16f, mzBase[1] * 0.16f, mzBase[2] * 0.16f };
            Equipment_AddSprite(0x47e, mzPos, kFormAuraHalo, mzHalo,
                                (intptr_t)puVar13, 0.0f, 0);
            float mzGround[3] = { mzLight[0] * 0.45f, mzLight[1] * 0.45f, mzLight[2] * 0.45f };
            Equipment_AddSprite(0x47e, mzPos, kFormAuraHalo * 0.739f, mzGround,
                                (intptr_t)puVar13, 0.0f, 0);
            float mzOff[3] = { 0.0f, 0.0f, 0.0f };
            float mzBody[3];
            BMD_TransformPosition(pvVar23,
                (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0 * 0x30),
                mzOff, mzBody, '\x01');
            Equipment_AddSprite(0x47e, mzBody, kFormAuraHalo * 0.5f, mzLight,
                                (intptr_t)puVar13, 0.0f, 0);
            float mzCore[3] = { mzBase[0] + (0.55f - mzBase[0]) * 0.45f,
                                mzBase[1] + (0.82f - mzBase[1]) * 0.45f,
                                mzBase[2] + (1.00f - mzBase[2]) * 0.45f };
            Equipment_AddSprite(0x47e, mzBody, kFormAuraHalo * 0.239f, mzCore,
                                (intptr_t)puVar13, 0.0f, 0);
            const int mzPhase = (int)(((intptr_t)puVar13 >> 5) & 0x3F);
            if ((((int)WorldTime + mzPhase) % 120) < 16) {
                float mzEmber[3] = { mzBase[0], mzBase[1], mzBase[2] };
                const float mzAng = (WorldTime + (float)mzPhase * 61.0f) *
                                    (2.0f * 3.14159265f / 2400.0f);
                const float mzRad = 23.0f * 18.0f;
                float emberPos[3] = { mzBody[0] + cosf(mzAng) * mzRad,
                                      mzBody[1] + sinf(mzAng) * mzRad,
                                      mzBody[2] };
                Particle_Spawn(0x4ab, emberPos, (float *)(puVar13 + 7),
                               mzEmber, 0, 0.9f, 0);
            }
        }

        // ── 1. Glove sparkle: class byte (cls&7)==0 + (cls_skin&0xF8)==8 ───
        // (DK 2nd-tier with specific skin range): sprite + sin halo at bone 19
        if (((*(BYTE *)&((CHARACTER*)(uintptr_t)(param_1))->Class & 7) == 0) &&
            ((*(char *)&((CHARACTER*)(uintptr_t)(param_1))->Skin & (char)~7) == '\b')) {
            local_48 = -4.0f; local_44 = 11.0f; local_40 = 0.0f;
            local_60 = 1.0f; local_5c = 1.0f; local_58 = 1.0f;
            BMD_TransformPosition(pvVar23, (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + 0x390),
                         &local_48, &local_54, '\x01');
            Equipment_AddSprite(0x498, &local_54, 0.6f, &local_60, 0, 0, 0);
            float fS = (float)(double)fsin((double)(WorldTime * _DAT_00552500));
            Equipment_AddSprite(0x4cf, &local_54, (float)(fS * _DAT_005528b4),
                         &local_60, 0, 0, 0);
        }

        // ── 2. PLAYER_SPELL anim (88 = 'X') — orbit sparkles (3 sprites) ──
        // IDA línea 2133-2152. Bone idx en c+628 (= LinkBone Weapon[0]),
        // tres Equipment_AddSprite(1191) con rotación basada en WorldTime.
        if (*(char *)&((OBJECT*)(uintptr_t)(puVar13))->CurrentAction == 'X') {
            BYTE bone = *&AsCharacter(puVar13)->Weapon[0].LinkBone;  // = (param_1+0x9d*4-1)? no: int-idx 0x9d * 4 = byte 0x274 = c+628 LinkBone[0]
            // Wait int-arithmetic: param_1 (int*) + 0x9d ints == byte+0x274 == c+628 ✓
            BMD_TransformPosition(pvVar23,
                (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + (UINT)bone * 0x30),
                &local_48, &local_54, '\x01');
            float Scalep = *(float *)&((OBJECT*)(uintptr_t)(puVar13))->AnimationFrame * 0.1f;  // anim frame * 0.1
            local_60 = 0.1f; local_5c = 0.1f; local_58 = 1.0f;
            Equipment_AddSprite(0x4a7, &local_54, Scalep * 0.30000001f,
                         &local_60, (intptr_t)puVar13, 0, 0);
            Equipment_AddSprite(0x4a7, &local_54, Scalep, (float*)(puVar13 + 0x3a),
                         (intptr_t)puVar13, -WorldTime * 0.1f, 0);
            Equipment_AddSprite(0x4a7, &local_54, Scalep * 2.5f, (float*)(puVar13 + 0x3a),
                         (intptr_t)puVar13, WorldTime * 0.1f, 0);
        }

        // ── 3. Dragon mode (entity flags bit 2) — fire spray from hand bones ──
        if ((*(BYTE *)(puVar13 + 0x1e) & 4) == 4) {
            BYTE *pbVar20 = &AsCharacter(puVar13)->Weapon[0].LinkBone;  // c+628 = first weapon LinkBone
            for (int iSlot = 2; iSlot > 0; iSlot--, pbVar20 += sizeof(PART_t)) {
                int iRnd = rand();
                local_60 = (float)(iRnd % 0x1e + 0x46) * _DAT_005524f8;
                local_5c = (float)local_60 * _DAT_005528b8;
                local_58 = (float)local_60 * _DAT_005526e4;
                BMD_TransformPosition(pvVar23,
                    (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + (UINT)*pbVar20 * 0x30),
                    &local_48, &local_54, '\x01');
                Equipment_AddSprite(0x4cf, &local_54, 1.5f, &local_60, (intptr_t)puVar13, 0, 0);
                BMD_TransformPosition(pvVar23,
                    (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + ((int)*pbVar20 - 6) * 0x30),
                    &local_48, &local_54, '\x01');
                Equipment_AddSprite(0x4cf, &local_54, 1.5f, &local_60, (intptr_t)puVar13, 0, 0);
                BMD_TransformPosition(pvVar23,
                    (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + ((int)*pbVar20 - 7) * 0x30),
                    &local_48, &local_54, '\x01');
                Equipment_AddSprite(0x4cf, &local_54, 1.5f, &local_60, (intptr_t)puVar13, 0, 0);
            }
        }

        // ── 4. Sin full set (v230==false) → no glow, exit ─────────────────
        // MEAMU NO-SHINE (regla de G, 2026-11-15): el brillo del set completo
        // (pasos 5 y 6: destellos en las armas + rayos del set +10/+11) queda
        // desactivado SIEMPRE — antes dependia del ajuste antilag del usuario.
        (void)v230;
        return puVar13;

        // ── 5. Full set body glow: PartObjectColor + 6 sparkles (3 bones × 2) ──
        // IDA línea 2191: PartObjectColor(c->Boot.Type, alpha, 0.5, Light, 0)
        // → escribe en Light[3] el RGB del glow basado en el tipo de bota.
        PartObjectColor(*(short*)&((CHARACTER*)(uintptr_t)(param_1))->BodyPart[5].Type,
                     *(float *)&((OBJECT*)(uintptr_t)(puVar13))->Alpha,
                     0.5f, &local_60, '\0');

        // Light = local_60..local_58 (RGB output del PartObjectColor)
        // Spawn 3 sprites por weapon slot (Weapon[0] y Weapon[1]) en bones
        // [bone, bone-6, bone-7] — esos son los huesos cercanos a la mano:
        //   bone (≈19/20)   = mano principal
        //   bone-6 (≈13/14) = brazo
        //   bone-7 (≈12/13) = hombro
        {
            BYTE *pbV193 = &AsCharacter(puVar13)->Weapon[0].LinkBone;  // c+628 = LinkBone[Weapon0]
            for (int v194 = 2; v194 > 0; v194--, pbV193 += sizeof(PART_t)) {
                BMD_TransformPosition(pvVar23,
                    (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + (UINT)*pbV193 * 0x30),
                    &local_48, &local_54, '\x01');
                Equipment_AddSprite(0x47e, &local_54, 1.3f, &local_60, (intptr_t)puVar13, 0, 0);  // CreateSprite 1150
                BMD_TransformPosition(pvVar23,
                    (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + ((int)*pbV193 - 6) * 0x30),
                    &local_48, &local_54, '\x01');
                Equipment_AddSprite(0x47e, &local_54, 1.3f, &local_60, (intptr_t)puVar13, 0, 0);
                BMD_TransformPosition(pvVar23,
                    (float *)((intptr_t)AsObject(puVar13)->BoneMatrices + ((int)*pbV193 - 7) * 0x30),
                    &local_48, &local_54, '\x01');
                Equipment_AddSprite(0x47e, &local_54, 1.3f, &local_60, (intptr_t)puVar13, 0, 0);
            }
        }

        // ── 5b. WATERFALL particles at bone 0 — REVERTIDO ───────────────────
        // Port de 5.2 quitado: 0.97k NO tiene esto, y el spawn rate provocaba
        // particle whiteout (todos accumulating sin morir). El +9 glow del
        // 0.97k es solo los 6 weapon-bone sprites + lightning crackle 1/20.

        // ── 6. EquipmentLevelSet >= 10 — extra lightning trail (1/20 chance) ──
        if (EquipmentLevelSet <= 9 || (rand() % 20)) {
            return puVar13;
        }
        // Save entity light, set to white temporarily for the lightning render
        float fSaveX = *(float *)(puVar13 + 0x3a);
        float fSaveY = *(float *)(puVar13 + 0x3b);
        float fSaveZ = *(float *)(puVar13 + 0x3c);
        *(float *)(puVar13 + 0x3a) = 1.0f;
        *(float *)(puVar13 + 0x3b) = 1.0f;
        *(float *)(puVar13 + 0x3c) = 1.0f;
        float *pfVar14 = (float *)(puVar13 + 0x3a);

        if (EquipmentLevelSet == 10) {
            // Single lightning particle around the entity
            Particle_Spawn(0x4e1, (float *)(puVar13 + 4),
                         (float *)(puVar13 + 7), pfVar14, 0, 0.19f, (intptr_t)puVar13);
        } else if (EquipmentLevelSet == 11) {
            // 1/8: lightning joint, 7/8: particle
            int v198 = rand() & 0x80000007;
            bool v197 = (v198 == 0);
            if (v198 < 0) {
                v197 = (((BYTE)v198 - 1) | 0xFFFFFFF8) == 0xFFu;
            }
            if (v197) {
                Joint_Create(0x4e1, (float *)(puVar13 + 4),
                             (float *)(puVar13 + 4), (float *)(puVar13 + 7),
                             0, (intptr_t)puVar13, 10.0f, -1, 0);
            } else {
                Particle_Spawn(0x4e1, (float *)(puVar13 + 4),
                             (float *)(puVar13 + 7), pfVar14, 0, 0.19f, (intptr_t)puVar13);
            }
        }
        // Restore entity light
        *(float *)(puVar13 + 0x3a) = fSaveX;
        *(float *)(puVar13 + 0x3b) = fSaveY;
        *(float *)(puVar13 + 0x3c) = fSaveZ;
        return puVar13;
    }

    } // end switch(sVar2)

    return puVar13;
}
