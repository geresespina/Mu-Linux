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
