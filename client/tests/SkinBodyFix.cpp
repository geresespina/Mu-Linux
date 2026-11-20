// MEAMU SKIN BODY FIX (2026-11-18) — rig del test de skin de cuerpo entero.
//
// Verifica el predicado REAL de producción (BijuTransform::IsFullBodySkinItem,
// definido en src/Render/BijuTransform.cpp — el target compila ese .cpp —,
// el mismo que alimenta NoteArmorItem)
// contra la tabla de casos que importa:
//
//   - las 4 skins vivas (8,50..53) y las futuras (54+) → true
//   - la armadura custom más alta que NO es skin (8,47 Majin Buu, y el
//     borde 8,49) → false: un set jamás apaga el cuerpo
//   - armaduras vanilla de la sección 8 (subs 0..21) → false
//   - otras secciones con sub >= 50 (casco, pets Biju) → false
//   - vacío (-1 / 0xFFFF) → false
//
// Los tipos crudos se arman con la fórmula del fork (ItemDefines.h):
// vanilla = sección*32+sub; agregado = 512 + sección*480 + (sub-32).
// (8,50) debe dar 4370, el mismo literal que JinchurikiSkinModel usa
// como kKyubiSkinType en BijuTransform.cpp.

#include "Render/BijuTransform.h"
#include <cstdio>
#include <cstdlib>

namespace
{
    constexpr int RawType(int section, int sub)
    {
        return (sub < 32) ? (section * 32 + sub)
                          : (512 + section * 480 + (sub - 32));
    }

    int s_failures = 0;

    void Expect(int itemType, bool want, const char* label)
    {
        const bool got = BijuTransform::IsFullBodySkinItem(itemType);
        if (got != want) {
            std::printf("FAIL %-28s type=%d want=%d got=%d\n", label, itemType, (int)want, (int)got);
            ++s_failures;
        } else {
            std::printf("ok   %-28s type=%d -> %d\n", label, itemType, (int)got);
        }
    }
}

int main()
{
    // Ancla de codificación: (8,50) en el espacio extendido = 4370.
    if (RawType(8, 50) != 4370) {
        std::printf("FAIL encoding anchor: RawType(8,50)=%d, expected 4370\n", RawType(8, 50));
        ++s_failures;
    }

    // Las 4 skins vivas + la familia futura.
    Expect(RawType(8, 50), true,  "Kyubi Skin (8,50)");
    Expect(RawType(8, 51), true,  "Konan Skin (8,51)");
    Expect(RawType(8, 52), true,  "Killer Bee Skin (8,52)");
    Expect(RawType(8, 53), true,  "Naruto Sage Skin (8,53)");
    Expect(RawType(8, 54), true,  "future skin (8,54)");
    Expect(RawType(8, 64), true,  "future skin (8,64)");
    Expect(RawType(8, 511), true, "last skin slot (8,511)");

    // Sets: jamás. El borde exacto y la custom más alta viva (8,47).
    Expect(RawType(8, 49), false, "edge (8,49)");
    Expect(RawType(8, 47), false, "Majin Buu Armor (8,47)");
    Expect(RawType(8, 44), false, "Tobi Armor (8,44)");
    Expect(RawType(8, 33), false, "first custom armor (8,33)");
    Expect(RawType(8, 21), false, "last vanilla armor (8,21)");
    Expect(RawType(8, 0),  false, "first vanilla armor (8,0)");

    // Otras secciones con sub alto no son armadura: no apagan nada.
    Expect(RawType(7, 50),  false, "helm section (7,50)");
    Expect(RawType(9, 60),  false, "pants section (9,60)");
    Expect(RawType(13, 496), false, "Biju pet (13,496)");
    Expect(RawType(12, 100), false, "wing/orb section (12,100)");

    // Vacío / centinelas de los feeds.
    Expect(-1,     false, "empty (-1)");
    Expect(0xFFFF, false, "empty (0xFFFF as int)");
    Expect((int)(short)0xFFFF, false, "empty (0xFFFF as short)");

    // ── MEAMU TRAIN 3 (2026-11-18): form-class tie-break rig ──────
    // The REAL production decision (BijuTransform::FormClassFor,
    // compiled from BijuTransform.cpp into this target): the shared
    // form bit 0x2000 resolves to the equipped beast's class, or to
    // Susanoo (228) when no soul pet is known.
    auto ExpectForm = [](bool bit, int beast, int want, const char* label) {
        const int got = BijuTransform::FormClassFor(bit, beast);
        if (got != want) {
            std::printf("FAIL form %-26s bit=%d beast=%d want=%d got=%d\n", label, (int)bit, beast, want, got);
            ++s_failures;
        } else {
            std::printf("ok   form %-26s bit=%d beast=%d -> %d\n", label, (int)bit, beast, got);
        }
    };
    ExpectForm(false, -1, -1,  "no bit, no pet");
    ExpectForm(false,  0, -1,  "no bit, Shukaku pet");
    ExpectForm(false,  9, -1,  "no bit, Ten-Tails pet");
    ExpectForm(true,   0, 155, "bit, Shukaku");
    ExpectForm(true,   4, 159, "bit, Kokuo");
    ExpectForm(true,   9, 164, "bit, Ten-Tails");
    ExpectForm(true,  -1, 228, "bit, NO pet = Susanoo");
    ExpectForm(true,  10, 228, "bit, beast OOB = Susanoo");

    // ── MEAMU TRAIN 7 (2026-11-18): personal-form rig ─────────
    // The REAL name-aware decision (BijuTransform::FormClassForNamed):
    // the pet-off titan (228) becomes Crimson Karane (229) ONLY for
    // Gloccy / M4D4R4; a known beast always wins over the name.
    auto ExpectFormNamed = [](bool bit, int beast, const char* name, int want, const char* label) {
        const int got = BijuTransform::FormClassForNamed(bit, beast, name);
        if (got != want) {
            std::printf("FAIL named %-28s bit=%d beast=%d name=%s want=%d got=%d\n", label, (int)bit, beast, name ? name : "(null)", want, got);
            ++s_failures;
        } else {
            std::printf("ok   named %-28s bit=%d beast=%d name=%s -> %d\n", label, (int)bit, beast, name ? name : "(null)", got);
        }
    };
    ExpectFormNamed(true,  -1, "Gloccy",     229, "bit, NO pet, Gloccy");
    ExpectFormNamed(true,  -1, "Eliger",     230, "bit, NO pet, Eliger (Train 67 Megazord)");
    ExpectFormNamed(true,   9, "Eliger",     164, "bit, Ten-Tails, Eliger (beast wins)");
    ExpectFormNamed(true,  -1, "M4D4R4",     229, "bit, NO pet, M4D4R4");
    ExpectFormNamed(true,  -1, "AnyoneElse", 228, "bit, NO pet, other name");
    ExpectFormNamed(true,  -1, "gloccy",     228, "bit, NO pet, lowercase");
    ExpectFormNamed(true,  -1, nullptr,      228, "bit, NO pet, no name");
    ExpectFormNamed(true,  10, "Gloccy",     229, "bit, beast OOB, Gloccy");
    ExpectFormNamed(true,   9, "Gloccy",     164, "bit, Ten-Tails, Gloccy");
    ExpectFormNamed(true,   0, "M4D4R4",     155, "bit, Shukaku, M4D4R4");
    ExpectFormNamed(false, -1, "Gloccy",      -1, "no bit, Gloccy");

    // ── MEAMU TRAIN 7 (2026-11-19): pose-less mode transforms ──
    // The cast-pose guard (BijuTransform::IsModeTransformSkill):
    // exactly the seven mode transforms cast with no pose; the
    // shared-body neighbours (48 Greater Life, 57 Omni Buff, 72
    // Biju Bond, 76 Biju Legion) and attack skills keep theirs.
    // -- MEAMU TRAIN 19 (2026-11-19): G's verdict 13:37 EST --
    // Biju Bond (72) joins the instant family; the pin flips.
    auto ExpectModeSkill = [](int skill, bool want, const char* label) {
        const bool got = BijuTransform::IsModeTransformSkill(skill);
        if (got != want) {
            std::printf("FAIL mode  %-28s skill=%d want=%d got=%d\n", label, skill, (int)want, (int)got);
            ++s_failures;
        } else {
            std::printf("ok   mode  %-28s skill=%d -> %d\n", label, skill, (int)got);
        }
    };
    ExpectModeSkill(58, true,  "Mangekyou Sharingan");
    ExpectModeSkill(59, true,  "Sage Mode");
    ExpectModeSkill(60, true,  "Jinchuriki Mode");
    ExpectModeSkill(61, true,  "Biju Mode");
    ExpectModeSkill(73, true,  "Chakra Cloak");
    ExpectModeSkill(74, true,  "Perfect Susanoo");
    ExpectModeSkill(75, true,  "Dragon Sage Mode");
    ExpectModeSkill(48, false, "Greater Life keeps pose");
    ExpectModeSkill(57, false, "Omni Buff keeps pose");
    ExpectModeSkill(72, true,  "Biju Bond (Train 19)");
    ExpectModeSkill(76, false, "Biju Legion keeps pose");
    ExpectModeSkill(70, false, "Chakra Roar keeps pose");
    ExpectModeSkill(41, false, "Twisting Slash keeps pose");

    // ── MEAMU TRAIN 8 ADDENDUM (2026-11-19): form-source rig ──
    // The REAL source-aware decision (BijuTransform::
    // FormClassForSource): G's ruling "Susanoo = always the
    // titan". With the source witnessed as 74, the pet slot is
    // ignored (228, or 229 for the two names); as 61 the beast
    // path is byte-identical to FormClassFor; unwitnessed (0)
    // the legacy Train 3/7 decision stands.
    auto ExpectFormSource = [](bool bit, int beast, const char* name, int source, int want, const char* label) {
        const int got = BijuTransform::FormClassForSource(bit, beast, name, source);
        if (got != want) {
            std::printf("FAIL source %-30s bit=%d beast=%d name=%s src=%d want=%d got=%d\n", label, (int)bit, beast, name ? name : "(null)", source, want, got);
            ++s_failures;
        } else {
            std::printf("ok   source %-30s bit=%d beast=%d name=%s src=%d -> %d\n", label, (int)bit, beast, name ? name : "(null)", source, got);
        }
    };
    // THE bug case: Susanoo cast WITH Kurama equipped.
    ExpectFormSource(true,  8, "Gloccy",     74, 229, "Susanoo+Kurama, Gloccy");
    ExpectFormSource(true,  8, "M4D4R4",     74, 229, "Susanoo+Kurama, M4D4R4");
    ExpectFormSource(true,  8, "AnyoneElse", 74, 228, "Susanoo+Kurama, other");
    ExpectFormSource(true,  0, "Gloccy",     74, 229, "Susanoo+Shukaku, Gloccy");
    ExpectFormSource(true, -1, "Gloccy",     74, 229, "Susanoo no pet, Gloccy");
    ExpectFormSource(true, -1, "AnyoneElse", 74, 228, "Susanoo no pet, other");
    ExpectFormSource(true, -1, nullptr,      74, 228, "Susanoo no pet, no name");
    // Biju Mode keeps the beast, exactly as today.
    ExpectFormSource(true,  8, "Gloccy",     61, 163, "Biju+Kurama, Gloccy");
    ExpectFormSource(true,  0, "M4D4R4",     61, 155, "Biju+Shukaku, M4D4R4");
    ExpectFormSource(true, -1, "Gloccy",     61, 228, "Biju, pet feed missed");
    // Unwitnessed: legacy decision, byte-identical.
    ExpectFormSource(true,  8, "Gloccy",      0, 163, "unknown+Kurama, Gloccy");
    ExpectFormSource(true, -1, "Gloccy",      0, 229, "unknown no pet, Gloccy");
    ExpectFormSource(true, -1, "AnyoneElse",  0, 228, "unknown no pet, other");
    ExpectFormSource(true,  8, "Gloccy",     99, 163, "bogus source = legacy");
    // Bit off: never a form, whatever the source says.
    ExpectFormSource(false, 8, "Gloccy",     74,  -1, "no bit, Susanoo src");
    ExpectFormSource(false, -1, "Gloccy",    61,  -1, "no bit, Biju src");

    if (s_failures) {
        std::printf("SKIN BODY FIX RIG: %d FAILURES\n", s_failures);
        return EXIT_FAILURE;
    }
    std::printf("SKIN BODY FIX RIG: ALL PASS\n");
    return EXIT_SUCCESS;
}

// ── Dobles de enlace (CI round 2, 2026-11-18) ─────────────────────────────
// El target compila src/Render/BijuTransform.cpp, donde vive la definición
// del predicado. Ese .cpp referencia servicios del juego que este rig no
// ejerce (sólo llama a IsFullBodySkinItem): dobles mínimos con las firmas
// exactas de globals.h / functions.h / Item/ContentCatalog.h. Precedente:
// TerrainDrawFlow define sus dobles en el propio archivo del test.
#include "Item/ContentCatalog.h"
#include <cstdint>

std::uintptr_t CharactersClient = 0;   // globals.h: base del pool de personajes
std::uintptr_t DAT_05828d58 = 0;       // globals.h: base de la tabla de modelos
char g_BoneScratch[200 * 0x30] = {};   // globals.h: scratch de huesos

CContentCatalog gContentCatalog;

const CatalogMonster* CContentCatalog::GetMonster(int) const { return nullptr; }
int CContentCatalog::GetItemModel(int) const { return -1; }

void BMD_Animation(void*, float*, float, unsigned int, unsigned char,
                   unsigned int*, float*, char, char) {}
void BMD__RenderBody(void*, int, float, int, float, float, float, int, int) {}
void Skeleton_Transform(void*, float*, float*, float*, float*, char) {}
void Entity_GetLightScale(std::uintptr_t, std::uintptr_t) {}
void operator_delete(void* ptr) { std::free(ptr); }

// MEAMU TRAIN 7: BijuTransform.cpp's Resolve llama a EntityName::Get;
// el rig no lo ejerce (solo llama a las decisiones puras), doble minimo.
#include "Entity/EntityName.h"
namespace EntityName { const char* Get(const void*) { return ""; } }
