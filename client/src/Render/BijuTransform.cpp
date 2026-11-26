// BijuTransform.cpp — ver BijuTransform.h para el diseño completo.
//
// Mapa de acciones (verificado contra los BMD reales y contra el código del
// cliente, 2026-11-16):
//
//   Convención de acciones de MONSTRUO en este cliente (SetPlayerWalk rama
//   no-jugador → 2; SetPlayerStop rama no-jugador → 0; SetPlayerDie → 6;
//   MoveCharacter sostiene la 6 como MONSTER01_DIE y devuelve 1/3/4/5/8/9
//   a 0 al terminar; SetMonsterSpeeds del catálogo escribe las velocidades
//   en ese mismo orden):
//       0 = stop/idle · 1 = stop2 · 2 = walk (única locomoción; en 9 de los
//       10 BMD es la acción con LockPositions) · 3 = attack1 · 4 = attack2
//       5 = shock/hurt · 6 = die · 7+ = extras (sólo Kokuo, 8 acciones)
//
//   Familias del JUGADOR (Character_Motion / BMD_Anim / NinjaRun):
//       die    = 131 · hit = 132 · attack/skill = 34-91 ·
//       walk   = 13-33 + 76/77 (nado con alas) + 79/80 (SetPlayerWalk /
//                NinjaRun::IsLocomotionAction) ·
//       idle   = todo lo demás: stops 0-12 (incl. montura 11/12 y vuelo
//                9/10), 78, sociales/saludos 92-130, 133-140 y los emotes
//                CustomAnim (>= 141, dances incluidas) → el biju queda en
//                idle mientras el jugador baila/saluda.
//
//   Fallbacks por bestia (desde el inventario real de cada BMD):
//       die:    acción 6 si existe; si no, la ÚLTIMA (Kurama tiene 6
//               acciones 0-5: su die es la 5 — el salvaje tiene el mismo
//               hueco: la 6 no existe en su BMD).
//       hit:    5 si existe; si no, attack (3).
//       attack: 3 si existe; si no, walk (2).
//       walk:   2 si existe; si no, 1; si no, 0.
//   Ninguna bestia cae en T-pose: toda acción mapeada se clampea a
//   [0, NumActions) y todo frame se remapea al rango de la acción destino.
//
// El tick de animación NO se toca (CharacterAnimation sigue avanzando el
// frame contra el modelo del jugador): la duración de ataques/skills y el
// contrato de retorno con MoveCharacter quedan byte-idénticos a stock.
// El remapeo de frames vive sólo en la pose: frame' = frame ×
// (framesBestia / framesJugador), con wrap en familias continuas (idle /
// walk) y clampa en one-shots (attack / hit / die, que así sostienen su
// frame final — el cadáver del biju sostiene la última pose de muerte).

#include "stdafx.h"
#include "Render/BijuTransform.h"
#include "Item/ItemDefines.h"   // GetItemSection / GetItemSub (espacio extendido)
#include "Model/ModelTypes.h"
#include "Physics/ClothTypes.h"   // ClothModelMesh (TRAIN 8: swap de triángulos de la skin)
#include "Entity/EntityTypes.h"
#include "Entity/EntityName.h"   // MEAMU TRAIN 7: nombre visible (FormClassForNamed)
#include "Item/ContentCatalog.h"
#include "globals.h"
#include "functions.h"
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" void DbgLogPublic(const char*);   // Train 65: hero form diagnostic

namespace BijuTransform
{
namespace
{
    // Ítems pet Biju: sección 13, subs 496-505 — continuación directa de
    // los pets custom ya vivos (Anivia 492, PuppyNivi 493, Bulbasaur 494,
    // Chopper 495).  OJO: el tipo CRUDO que llega a NotePetItem vive en el
    // espacio de índices EXTENDIDO del fork (ItemDefines.h: vanilla =
    // sección*32+sub; agregados = 512 + sección*480 + (sub-32)), así que
    // (13,496) llega como 512+13*480+464 = 7216 y (13,504) "Kurama
    // (Biju Pet)" como 7224.  La comparación anterior
    // contra 13*512+496 = 7152..7161 (espacio vanilla inexistente en este
    // fork) no matcheaba NUNCA: la transformación no disparaba jamás.
    // Se resuelve por (sección, sub) con el mapeo propio del cliente.
    constexpr int kBeastPetSection = 13;
    constexpr int kFirstBeastSub   = 496;
    constexpr int kBeastCount      = 10;
    constexpr int kFirstBeastClass = 155;             // Shukaku .. Ten-Tails
    constexpr int kSusanooClass    = 228;
    constexpr int kKaraneCrimsonClass = 229;
    constexpr int kMegazordClass = 230;
    constexpr int kDragonFormClass = 231;
    constexpr int kThousandHandsClass = 232;  // MEAMU TRAIN 109: Thousand Hands (skill 82) -- the Oozaru Gold model as the Senju evolved form (server class 232)   // MEAMU TRAIN 99: Dragon Form (skill 77) -- the Kaido boss model (server class 193) as the Senju form   // MEAMU TRAIN 67: Eliger's Blue Megazord (skill 74)          // MEAMU TRAIN 7: Crimson Karane, forma personal de G (skill 74, nombres Gloccy/M4D4R4)             // MEAMU TRAIN 3: Perfect Susanoo form (skill 74)

    signed char s_beastBySlot[400];
    // MEAMU SKIN BODY FIX (2026-11-18): skin de cuerpo entero puesta en el
    // slot de armadura, por slot del pool (1 = puesta). Tabla hermana de
    // la del pet: mismos feeds, mismo Clear.
    signed char s_skinBySlot[400];
    // MEAMU TRAIN 8 ADDENDUM (2026-11-19): que skill produjo la forma
    // de cada slot (61 = Biju Mode, 74 = Perfect Susanoo, 0 = nunca
    // presenciado). Tabla hermana de la del pet: la alimentan el
    // paquete de casteo 0x19 (eco del heroe + remotos) y el feed de
    // efectos F3/F1 del heroe; la limpia el mismo Clear.
    unsigned char s_formSourceBySlot[400];
    // MEAMU TRAIN 104 (2026-11-21, G's live report): the slot table
    // dies with the character object -- Entity_Spawn Clear() wipes
    // it on every respawn (teleport, map move), while the form BIT
    // lives in the state word and survives. On re-resolution the
    // source read 0 and the form fell through to the legacy
    // pet/named fallback (the dragon became Susanoo/beast after a
    // teleport). This sister table is keyed by the character's
    // visible NAME, which no respawn changes: NoteFormSource feeds
    // it, ForgetFormSource (form effect loss) erases it, and the
    // resolver consults it whenever the slot read is empty.
    struct FormSourceByName { char name[16]; unsigned char source; };
    FormSourceByName s_formSourceByName[64];
    int              s_formSourceByNameCount = 0;

    void RecordFormSourceByName(const char* name, int skillType)
    {
        if (!name || !name[0]) return;
        for (int i = 0; i < s_formSourceByNameCount; ++i) {
            if (std::strncmp(s_formSourceByName[i].name, name, 15) == 0) {
                s_formSourceByName[i].source = (unsigned char)skillType;
                return;
            }
        }
        if (s_formSourceByNameCount >= 64) return;
        FormSourceByName& e = s_formSourceByName[s_formSourceByNameCount++];
        std::memset(e.name, 0, sizeof(e.name));
        std::strncpy(e.name, name, 15);
        e.source = (unsigned char)skillType;
    }

    int LookupFormSourceByName(const char* name)
    {
        if (!name || !name[0]) return 0;
        for (int i = 0; i < s_formSourceByNameCount; ++i) {
            if (std::strncmp(s_formSourceByName[i].name, name, 15) == 0)
                return (int)s_formSourceByName[i].source;
        }
        return 0;
    }

    void EraseFormSourceByName(const char* name)
    {
        if (!name || !name[0]) return;
        for (int i = 0; i < s_formSourceByNameCount; ++i) {
            if (std::strncmp(s_formSourceByName[i].name, name, 15) == 0) {
                s_formSourceByName[i] = s_formSourceByName[--s_formSourceByNameCount];
                return;
            }
        }
    }
    bool        s_init = false;

    void EnsureInit()
    {
        if (!s_init) {
            std::memset(s_beastBySlot, -1, sizeof(s_beastBySlot));
            std::memset(s_skinBySlot, 0, sizeof(s_skinBySlot));
            std::memset(s_formSourceBySlot, 0, sizeof(s_formSourceBySlot));
            s_init = true;
        }
    }

    int SlotOf(const void* character)
    {
        if (!character || !DAT_07abf5d0) return -1;
        const uintptr_t base = (uintptr_t)DAT_07abf5d0;
        const uintptr_t addr = (uintptr_t)character;
        if (addr < base) return -1;
        const uintptr_t off = addr - base;
        if (off >= 400u * (uintptr_t)CHARACTER_STRIDE ||
            off % (uintptr_t)CHARACTER_STRIDE != 0) return -1;
        return (int)(off / (uintptr_t)CHARACTER_STRIDE);
    }

    // Resuelve clase → (modelo, escala) del catálogo. El modelo es el MISMO
    // slot que usa la bestia salvaje: si el server cambia el modelo o la
    // escala de una clase, la transformación lo sigue sin tocar código.
    bool Resolve(const CHARACTER* character, int* modelOut, float* scaleOut)
    {
        if (!character) return false;
        const OBJECT* o = AsObject(character);
        if (o->Type != 390 || o->Kind != 1) return false;   // sólo jugadores
        // MEAMU TRAIN 4 (2026-11-17, follow-up): la FORMA ya no se dispara
        // por el pet equipado solo (comportamiento v1). El cuerpo-bestia se
        // renderiza SOLO con el modo Biju activo: bit 0x2000 del DWORD de
        // estado persistente en entity+120 — el mismo canal que el aura
        // Jinchuriki (0x1000), Mangekyou (0x0400) y Sage (0x0800), que
        // InsertBuffPhysicalEffect llena por F3:F1/F3:F2 al entrar en el
        // viewport y por C1:07 al castear, para el héroe y para los
        // remotos por igual. Resolve es el único cuello de botella de
        // ModelForCharacter / PreparePose / DrawBody, así que esta puerta
        // apaga a la vez el cuerpo bestia, la pose bestia, el apagado de
        // los renders humanos (RenderCharacter) y el ocultado del bug del
        // pet equipado (Entity_Render): pet equipado sin modo = humano
        // normal con su pet visible, como antes del sistema transform.
        const bool formBit =
            (*(const DWORD*)((const BYTE*)character + 120) & 0x2000) != 0;
        if (!formBit)
            return false;
        EnsureInit();
        const int slot = SlotOf(character);
        if (slot < 0) return false;
        // MEAMU TRAIN 3 (2026-11-18): tie-break on the shared form
        // bit (see FormClassFor in the header): pet known -> that beast's
        // class (existing behaviour, byte-identical); no pet known ->
        // Perfect Susanoo, class 228. A missing/unloaded 228 catalog
        // model falls back to the normal player render below, exactly
        // like a missing beast model does today.
        // MEAMU TRAIN 7 (2026-11-18): la decision pasa por la variante
        // con nombre -- Gloccy/M4D4R4 sin pet resuelven a la clase
        // 229 (Crimson Karane) en vez de la 228; el resto, identico.
        // MEAMU TRAIN 8 ADDENDUM (2026-11-19): la decision pasa por
        // FormClassForEntity -- la misma que usan las auras de forma
        // (CharacterEffects 0g/0h) -- que ademas conoce la skill de
        // origen: Susanoo (74) con pet rinde el titan (228/229),
        // nunca la bestia; Biju Mode (61) sigue con la bestia.
        const int formClass = FormClassForEntity(character);
        if (formClass < 0) return false;
        const CatalogMonster* cm = gContentCatalog.GetMonster(formClass);
        // Train 61: la forma carga su modelo la primera vez que se pide.
        if (cm && cm->ModelPending) gContentCatalog.EnsureMonsterModel(formClass);
        if (!cm || cm->Model < 0) {
            // Train 65: hero diagnostic -- name the failing gate in
            // the debug log, once per verdict change.
            if (slot == 400) {
                static int s_lastFail = -999;
                const int failCode = cm ? -2 : -3;   // -2 model failed, -3 class absent
                if (s_lastFail != failCode * 1000 + formClass) {
                    s_lastFail = failCode * 1000 + formClass;
                    char buf[192];
                    std::snprintf(buf, sizeof(buf),
                        "BijuTransform hero form FAIL: class=%d cm=%s model=%d (Train 65 diag)",
                        formClass, cm ? "present" : "ABSENT", cm ? cm->Model : -99);
                    DbgLogPublic(buf);
                }
            }
            return false;
        }
        const BMD* bm = AsModel((uintptr_t)DAT_05828d58) + cm->Model;
        if (bm->NumActions <= 0 || bm->NumMeshs <= 0 || !bm->Actions || !bm->Bones) {
            if (slot == 400) {
                static int s_lastBmdFail = -999;
                if (s_lastBmdFail != formClass) {
                    s_lastBmdFail = formClass;
                    char buf[192];
                    std::snprintf(buf, sizeof(buf),
                        "BijuTransform hero form FAIL: class=%d model=%d BMD empty (actions=%d meshs=%d) (Train 65 diag)",
                        formClass, cm->Model, (int)bm->NumActions, (int)bm->NumMeshs);
                    DbgLogPublic(buf);
                }
            }
            return false;   // modelo no cargado → render normal del jugador
        }
        if (slot == 400) {
            static int s_lastOk = -999;
            if (s_lastOk != formClass) {
                s_lastOk = formClass;
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                    "BijuTransform hero form OK: class=%d model=%d (Train 65 diag)",
                    formClass, cm->Model);
                DbgLogPublic(buf);
            }
        }
        if (modelOut) *modelOut = cm->Model;
        if (scaleOut) *scaleOut = (cm->Scale > 0.0f) ? cm->Scale : 1.0f;
        return true;
    }

    enum Family { FamIdle, FamWalk, FamAttack, FamHit, FamDie };

    Family PlayerFamily(int action)
    {
        if (action == 131) return FamDie;
        if (action == 132) return FamHit;
        if (action >= 34 && action <= 91) return FamAttack;
        if ((action >= 13 && action <= 33) || action == 76 || action == 77 ||
            action == 79 || action == 80) return FamWalk;
        return FamIdle;
    }

    int BeastAction(const BMD* bm, Family fam)
    {
        const int n = (int)bm->NumActions;
        if (n <= 0) return 0;
        auto has = [n](int i) { return i < n; };
        switch (fam) {
            case FamDie:    return has(6) ? 6 : n - 1;
            case FamHit:    return has(5) ? 5 : (has(3) ? 3 : 0);
            case FamAttack: return has(3) ? 3 : (has(2) ? 2 : 0);
            case FamWalk:   return has(2) ? 2 : (has(1) ? 1 : 0);
            case FamIdle:
            default:        return 0;
        }
    }

    int ActionFrames(const BMD* model, int action)
    {
        if (!model || !model->Actions || action < 0 || action >= model->NumActions)
            return 0;
        return (int)reinterpret_cast<const ModelAnimationAction*>(model->Actions)[action].FrameCount;
    }

    float RemapFrame(const BMD* playerModel, int playerAction, float frame,
                     const BMD* bm, int beastAction, Family fam)
    {
        const int pf = ActionFrames(playerModel, playerAction);
        const int bf = ActionFrames(bm, beastAction);
        if (pf <= 0 || bf <= 0) return 0.0f;
        float f = frame * ((float)bf / (float)pf);
        if (fam == FamIdle || fam == FamWalk) {
            f = std::fmod(f, (float)bf);
            if (f < 0.0f) f += (float)bf;
        } else {
            const float hi = (float)bf - 0.01f;
            if (f > hi) f = hi;
            if (f < 0.0f) f = 0.0f;
        }
        return f;
    }
}

void NotePetItem(void* character, int itemType)
{
    EnsureInit();
    const int slot = SlotOf(character);
    if (slot < 0) return;
    // Resolver por (sección, sub) con el mapeo del propio cliente: el tipo
    // crudo viene en el espacio extendido (ver ItemDefines.h), el mismo
    // con el que el catálogo indexa el ítem (por eso su icono sí dibuja).
    int beast = -1;
    if (itemType >= 0 && GetItemSection(itemType) == kBeastPetSection) {
        const int sub = GetItemSub(itemType);
        if (sub >= kFirstBeastSub && sub < kFirstBeastSub + kBeastCount)
            beast = sub - kFirstBeastSub;
    }
    s_beastBySlot[slot] = (signed char)beast;
}

void Clear(void* character)
{
    EnsureInit();
    const int slot = SlotOf(character);
    if (slot >= 0) {
        s_beastBySlot[slot] = -1;
        s_skinBySlot[slot] = 0;
        s_formSourceBySlot[slot] = 0;
    }
}

void PreloadFormModels()
{
    // Train 65: load (or retry a cached failure of) every form
    // class NOW, in cast/packet context -- never leave the load
    // to Resolve() mid-render. See the header for the bug.
    static const int kFormClasses[] = { kSusanooClass, kKaraneCrimsonClass, kMegazordClass, kDragonFormClass, kThousandHandsClass };   // + Thousand Hands (Train 109)
    for (int cls : kFormClasses) {
        const CatalogMonster* cm = gContentCatalog.GetMonster(cls);
        if (!cm) continue;
        if (cm->ModelPending) gContentCatalog.EnsureMonsterModel(cls);
        else if (cm->Model < 0) gContentCatalog.RetryMonsterModel(cls);
    }
    for (int beast = 0; beast < kBeastCount; ++beast) {
        const int cls = kFirstBeastClass + beast;
        const CatalogMonster* cm = gContentCatalog.GetMonster(cls);
        if (!cm) continue;
        if (cm->ModelPending) gContentCatalog.EnsureMonsterModel(cls);
        else if (cm->Model < 0) gContentCatalog.RetryMonsterModel(cls);
    }
}

void NoteFormSource(void* character, int skillType)
{
    // Solo las skills de forma comparten el bit 0x2000; cualquier
    // otro valor no es un origen de forma y se ignora.
    // MEAMU TRAIN 99: skill 77 (Dragon Form) joins 61/74.
    if (skillType != 61 && skillType != 74 && skillType != 77 && skillType != 82) return;   // + 82 Thousand Hands (Train 109 fix: the witness gate itself -- without it the source is never recorded and the form falls through to the legacy 228 fallback)
    EnsureInit();
    const int slot = SlotOf(character);
    if (slot < 0) return;
    s_formSourceBySlot[slot] = (unsigned char)skillType;
    RecordFormSourceByName(EntityName::Get((const CHARACTER*)character), skillType);   // TRAIN 104: durable across respawns
    PreloadFormModels();   // Train 65: a form was just witnessed -- load its models now
}

void ForgetFormSource(void* character)
{
    // TRAIN 104: the form effect ended -- drop both records so a
    // later form on this character starts from a clean slate (its
    // own cast witness re-records the new source).
    EnsureInit();
    const int slot = SlotOf(character);
    if (slot >= 0) s_formSourceBySlot[slot] = 0;
    EraseFormSourceByName(EntityName::Get((const CHARACTER*)character));
}

bool IsFullBodySkinItem(int itemType)
{
    // Definición (declarada en BijuTransform.h): la decodificación
    // (sección, sub) del cliente vive acá porque ItemDefines.h incluye
    // PlatformHeaders.h y el header no debe arrastrarlo (CI round 2).
    return itemType >= 0 && GetItemSection(itemType) == 8 &&
           GetItemSub(itemType) >= 50;
}

void NoteArmorItem(void* character, int itemType)
{
    EnsureInit();
    const int slot = SlotOf(character);
    if (slot < 0) return;
    s_skinBySlot[slot] = IsFullBodySkinItem(itemType) ? 1 : 0;
}

bool WearsFullBodySkin(const CHARACTER* character)
{
    EnsureInit();
    const int slot = SlotOf(character);
    return slot >= 0 && s_skinBySlot[slot] != 0;
}

int BeastFor(const CHARACTER* character)
{
    EnsureInit();
    const int slot = SlotOf(character);
    return (slot >= 0) ? (int)s_beastBySlot[slot] : -1;
}

int FormClassFor(bool formBit, int beastIndex)
{
    // MEAMU TRAIN 3 (2026-11-18): pure tie-break for the shared form
    // bit 0x2000 -- see the header. Kept free of catalog/model state
    // so tests/SkinBodyFix.cpp can rig it headlessly.
    if (!formBit) return -1;
    if (beastIndex >= 0 && beastIndex < kBeastCount)
        return kFirstBeastClass + beastIndex;
    return kSusanooClass;
}

int FormClassForNamed(bool formBit, int beastIndex, const char* name)
{
    // MEAMU TRAIN 7 (2026-11-18): extension con nombre de
    // FormClassFor -- ver el header. Solo el titan sin pet (228)
    // es desviable, y solo para los dos nombres de G; la bestia
    // (pet equipado) y el bit apagado jamas se tocan.
    const int base = FormClassFor(formBit, beastIndex);
    if (base != kSusanooClass || !name) return base;
    if (std::strcmp(name, "Gloccy") == 0 ||
        std::strcmp(name, "M4D4R4") == 0)
        return kKaraneCrimsonClass;
    // MEAMU TRAIN 67 (2026-11-20, G's order): Eliger's Susanoo is
    // the BLUE MEGAZORD -- the HurricaneBlue skin BMD (already in
    // every client's Data as the (8,55) hero skin) registered as
    // monster class 230 at titan scale. Same titan-path gate as
    // the Karane diversion: only a pet-less / source-74 titan
    // resolution diverts; a known beast still wins.
    if (std::strcmp(name, "Eliger") == 0)
        return kMegazordClass;
    return base;
}

int FormClassForSource(bool formBit, int beastIndex, const char* name,
                       int sourceSkill)
{
    // MEAMU TRAIN 8 ADDENDUM (2026-11-19, G's ruling: "Susanoo =
    // always the titan"): con la skill de origen conocida, el pet
    // deja de ser el desempate. Susanoo (74) ignora el slot de pet
    // por completo -- el camino del titan es FormClassForNamed con
    // la bestia forzada a desconocida (228, o 229 para los dos
    // nombres). Biju Mode (61) es exactamente FormClassFor: la
    // bestia conocida manda, y sin bestia conocida cae al 228 de
    // respaldo, como hoy. Sin origen conocido (0 u otro), la
    // decision heredada de Train 3/7 queda byte-identica.
    if (!formBit) return -1;
    if (sourceSkill == 82)
        return kThousandHandsClass;
    if (sourceSkill == 77)
        return kDragonFormClass;
    if (sourceSkill == 74)
        return FormClassForNamed(true, -1, name);
    if (sourceSkill == 61)
        return FormClassFor(true, beastIndex);
    return FormClassForNamed(true, beastIndex, name);
}

int FormClassForEntity(const CHARACTER* character)
{
    // La decision completa de render de forma para una entidad:
    // bit compartido 0x2000 + tablas laterales (pet, origen) +
    // nombre visible. Resolve() y las auras 0g/0h pasan por aca.
    if (!character) return -1;
    const bool formBit =
        (*(const DWORD*)((const BYTE*)character + 120) & 0x2000) != 0;
    if (!formBit) return -1;
    EnsureInit();
    const int slot = SlotOf(character);
    const int beast = (slot >= 0) ? (int)s_beastBySlot[slot] : -1;
    int source = (slot >= 0) ? (int)s_formSourceBySlot[slot] : 0;
    if (source == 0)   // TRAIN 104: slot record died with a respawn -- ask the name table
        source = LookupFormSourceByName(EntityName::Get(character));
    return FormClassForSource(true, beast, EntityName::Get(character),
                              source);
}

bool IsModeTransformSkill(int skillType)
{
    // MEAMU TRAIN 7 (2026-11-19): ver el header -- exactamente los
    // siete toggles de modo; 57/72/76 comparten cuerpos de casteo
    // con ellos y NO estan: conservan su pose.
    // MEAMU TRAIN 19 (2026-11-19): G's verdict 13:37 EST -- the
    // WHOLE mode family fires instant, Biju Bond (72) included;
    // Train 7's exclusion of 72 is superseded. 57/76 siguen
    // fuera: conservan su pose.
    return skillType == 58 || skillType == 59 || skillType == 60 ||
           skillType == 61 || skillType == 72 || skillType == 73 ||
           skillType == 74 || skillType == 75 || skillType == 77 ||
           skillType == 82;  // MEAMU TRAIN 109: Thousand Hands -- pose-less mode toggle like the rest of the family
}

bool JinchurikiTint(const CHARACTER* character, float out[3])
{
    // G's colour table (2026-11-17), tails 1-10 = beast index 0-9:
    // Shukaku sand-gold, Matatabi blue, Isobu teal, Son Goku lava red,
    // Kokuo steam white, Saiken pink, Chomei green, Gyuki violet,
    // Kurama orange-gold, Ten-Tails pale grey-green.
    static const float kTint[10][3] = {
        { 0.851f, 0.643f, 0.255f },  // 1 Shukaku  #D9A441
        { 0.243f, 0.482f, 0.980f },  // 2 Matatabi #3E7BFA
        { 0.184f, 0.702f, 0.651f },  // 3 Isobu    #2FB3A6
        { 0.886f, 0.282f, 0.239f },  // 4 Son Goku #E2483D
        { 0.929f, 0.937f, 0.949f },  // 5 Kokuo    #EDEFF2
        { 0.949f, 0.655f, 0.765f },  // 6 Saiken   #F2A7C3
        { 0.478f, 0.780f, 0.310f },  // 7 Chomei   #7AC74F
        { 0.557f, 0.424f, 0.784f },  // 8 Gyuki    #8E6CC8
        { 0.961f, 0.522f, 0.122f },  // 9 Kurama   #F5851F
        { 0.725f, 0.769f, 0.694f },  // 10 Ten-Tails #B9C4B1
    };
    const int beast = BeastFor(character);
    if (beast < 0 || beast >= kBeastCount || !out) return false;
    out[0] = kTint[beast][0];
    out[1] = kTint[beast][1];
    out[2] = kTint[beast][2];
    return true;
}

int ModelForCharacter(const CHARACTER* character)
{
    int model = -1;
    return Resolve(character, &model, nullptr) ? model : -1;
}

int JinchurikiSkinModel(const CHARACTER* character)
{
    if (!character) return -1;
    const OBJECT* o = AsObject(character);
    if (o->Type != 390 || o->Kind != 1) return -1;   // sólo jugadores
    const DWORD state = *(const DWORD*)((const BYTE*)character + 120);
    if ((state & 0x1000) == 0) return -1;           // Jinchuriki Mode off
    if ((state & 0x2000) != 0) return -1;           // Biju Mode: transform wins
    // Naruto Kyubi Skin = item (8,50). El tipo crudo vive en el espacio
    // extendido del fork: 512 + sección*480 + (sub-32) = 512+3840+18.
    constexpr int kKyubiSkinType = 512 + 8 * 480 + (50 - 32);   // 4370
    const int model = gContentCatalog.GetItemModel(kKyubiSkinType);
    if (model >= 0) {
        const BMD* bm = AsModel((uintptr_t)DAT_05828d58) + model;
        if (bm->NumActions > 0 && bm->NumMeshs > 0 && bm->Actions && bm->Bones)
            return model;
    }
    // MEAMU TRAIN 123 (2026-11-24): on G's client the catalog route
    // resolves but the BMD never lands (aura renders, body does not --
    // the booth, same code + same server, renders). Stop depending on
    // the catalog's pending machinery for this one asset: load the
    // known file directly through the same loader, once, and log the
    // outcome so the debug.log names the stage either way.
    static int  s_RetryModel = -2;
    static bool s_RetryLogged = false;
    if (s_RetryModel == -2) {
        s_RetryModel = gContentCatalog.RetryItemModel(kKyubiSkinType);
        if (!s_RetryLogged) {
            s_RetryLogged = true;
            char dbg[128];
            std::snprintf(dbg, sizeof(dbg), "JinchurikiSkin: catalog model=%d, retry load=%d", model, s_RetryModel);
            DbgLogPublic(dbg);
        }
    }
    if (s_RetryModel >= 0) {
        const BMD* bm = AsModel((uintptr_t)DAT_05828d58) + s_RetryModel;
        if (bm->NumActions > 0 && bm->NumMeshs > 0 && bm->Actions && bm->Bones)
            return s_RetryModel;
    }
    return -1;   // modelo no cargado: render normal del jugador
    return model;
}


void JinchurikiGlowBegin(const CHARACTER* character, float savedLight[3])
{
    if (!character || !savedLight) return;
    OBJECT* o = AsObject((void*)character);
    savedLight[0] = o->Light[0];
    savedLight[1] = o->Light[1];
    savedLight[2] = o->Light[2];
    float tint[3] = { 0.961f, 0.522f, 0.122f };   // Kurama gold (fallback, as the aura)
    JinchurikiTint(character, tint);
    // Same 1200 ms pulse as the Train 4 aura, so body and aura breathe together.
    const float jt = 0.5f - 0.5f * cosf(WorldTime * (2.0f * 3.14159265f / 1200.0f));
    const float boost = 1.25f * (0.82f + 0.30f * jt);
    o->Light[0] = tint[0] * boost;
    o->Light[1] = tint[1] * boost;
    o->Light[2] = tint[2] * boost;
}

void JinchurikiGlowEnd(const CHARACTER* character, const float savedLight[3])
{
    if (!character || !savedLight) return;
    OBJECT* o = AsObject((void*)character);
    o->Light[0] = savedLight[0];
    o->Light[1] = savedLight[1];
    o->Light[2] = savedLight[2];
}

namespace
{
    // ── TRAIN 8: filtro de cabeza de la skin Kyubi ──────────────────────
    // Análisis del asset (SkinNarutoKyubi.bmd, sha256 995af166…, ver
    // TRAIN8-REPORT.md): UNA malla de 10.160 vértices / 18.688 triángulos.
    // Los vértices de la cabeza (cráneo, cara, orejas y pelo) están
    // ligados a los huesos 20/22/23 del esqueleto Bip01 del propio
    // modelo ('Bip01 Head' / 'Bip01 Ponytail1' / 'Bip01 Ponytail11'),
    // con Z mundial en pose bind [160,7 .. 206,4] — sobre el cuello
    // (hueso 19, origen Z 156,2). 7.953 triángulos tienen sus 3 vértices
    // en huesos de cabeza: ésa es la cáscara que encerraba la cabeza
    // real del jugador. Los 41 triángulos mixtos (vértices de cabeza +
    // Spine1) son la costura cuello/torso y se CONSERVAN.
    bool  s_skinHeadTried = false;    // build intentado (una sola vez)
    bool  s_skinHeadActive = false;   // swap aplicado ahora mismo
    int   s_skinHeadModel = -1;
    BYTE* s_skinHeadTris = nullptr;   // copia filtrada (10.735 tris)
    short s_skinHeadTriCount = 0;
    BYTE* s_skinOrigTris = nullptr;   // respaldo para restaurar
    short s_skinOrigTriCount = 0;

    bool BuildSkinHeadFilter(int modelIdx)
    {
        BMD* bm = AsModel((uintptr_t)DAT_05828d58) + modelIdx;
        if (bm->NumMeshs != 1 || !bm->Meshs || !bm->Bones) return false;
        auto* mesh = reinterpret_cast<ClothModelMesh*>(bm->Meshs);
        // Perfil exacto de la skin despachada: si el asset cambia, la
        // verificación de abajo no cierra y no se filtra nada.
        if (mesh->VertexCount != 10160 || mesh->TriangleCount != 18688 ||
            !mesh->Vertices || !mesh->Triangles)
            return false;
        // Huesos de cabeza por NOMBRE desde el esqueleto del modelo.
        auto* bones = reinterpret_cast<ModelAnimationBone*>(bm->Bones);
        bool headBone[512] = {};
        int found = 0;
        for (int b = 0; b < bm->NumBones && b < 512; ++b) {
            if (std::strcmp(bones[b].Name, "Bip01 Head") == 0 ||
                std::strcmp(bones[b].Name, "Bip01 Ponytail1") == 0 ||
                std::strcmp(bones[b].Name, "Bip01 Ponytail11") == 0) {
                headBone[b] = true;
                ++found;
            }
        }
        if (found != 3) return false;
        const auto* verts = reinterpret_cast<const ModelRuntimeVertex*>(mesh->Vertices);
        const auto* tris  = reinterpret_cast<const ModelRuntimeTriangle*>(mesh->Triangles);
        BYTE* out = static_cast<BYTE*>(
            operator_new((std::size_t)mesh->TriangleCount * sizeof(ModelRuntimeTriangle)));
        if (!out) return false;
        int kept = 0, dropped = 0;
        for (int f = 0; f < mesh->TriangleCount; ++f) {
            const ModelRuntimeTriangle& t = tris[f];
            const short b0 = verts[t.VertexIndex[0]].Bone;
            const short b1 = verts[t.VertexIndex[1]].Bone;
            const short b2 = verts[t.VertexIndex[2]].Bone;
            const bool head =
                b0 >= 0 && b0 < 512 && headBone[b0] &&
                b1 >= 0 && b1 < 512 && headBone[b1] &&
                b2 >= 0 && b2 < 512 && headBone[b2];
            if (head) { ++dropped; continue; }
            std::memcpy(out + (std::size_t)kept * sizeof(ModelRuntimeTriangle),
                        &t, sizeof(ModelRuntimeTriangle));
            ++kept;
        }
        // Cierre contra el análisis del asset: 7.953 fuera / 10.735 dentro.
        // Otro número = asset distinto → no tocar el dibujo.
        if (dropped != 7953 || kept != 10735) {
            operator_delete(out);
            return false;
        }
        s_skinHeadTris = out;
        s_skinHeadTriCount = (short)kept;
        s_skinHeadModel = modelIdx;
        return true;
    }
}   // namespace

void SkinHeadlessBegin(int modelIdx)
{
    if (s_skinHeadActive) return;
    if (!s_skinHeadTried) {
        s_skinHeadTried = true;
        BuildSkinHeadFilter(modelIdx);   // deja s_skinHeadTris listo o null
    }
    if (!s_skinHeadTris || modelIdx != s_skinHeadModel) return;
    BMD* bm = AsModel((uintptr_t)DAT_05828d58) + modelIdx;
    if (bm->NumMeshs != 1 || !bm->Meshs) return;
    auto* mesh = reinterpret_cast<ClothModelMesh*>(bm->Meshs);
    // Identidad en cada swap: si el modelo se recargó, Meshs/Triangles ya
    // no son los del perfil verificado → no tocar.
    if (mesh->TriangleCount != 18688 || !mesh->Triangles) return;
    s_skinOrigTris = mesh->Triangles;
    s_skinOrigTriCount = mesh->TriangleCount;
    mesh->Triangles = s_skinHeadTris;
    mesh->TriangleCount = s_skinHeadTriCount;
    s_skinHeadActive = true;
}

void SkinHeadlessEnd(int modelIdx)
{
    if (!s_skinHeadActive || modelIdx != s_skinHeadModel) return;
    BMD* bm = AsModel((uintptr_t)DAT_05828d58) + modelIdx;
    if (bm->NumMeshs == 1 && bm->Meshs) {
        auto* mesh = reinterpret_cast<ClothModelMesh*>(bm->Meshs);
        mesh->Triangles = s_skinOrigTris;
        mesh->TriangleCount = s_skinOrigTriCount;
    }
    s_skinHeadActive = false;
}

bool PreparePose(OBJECT* entity, float* bones, char translate)
{
    int modelIdx = -1;
    float beastScale = 1.0f;
    if (!Resolve((const CHARACTER*)entity, &modelIdx, &beastScale)) return false;

    BMD* bm = AsModel((uintptr_t)DAT_05828d58) + modelIdx;
    const BMD* playerModel = AsModel((uintptr_t)DAT_05828d58) + 390;

    const int cur = (int)entity->CurrentAction;
    const int pri = (int)entity->PriorAction;
    const Family fam = PlayerFamily(cur);
    const Family priFam = PlayerFamily(pri);
    const int mapped    = BeastAction(bm, fam);
    int mappedPri = BeastAction(bm, priFam);
    const float frame = RemapFrame(playerModel, cur, entity->AnimationFrame, bm, mapped, fam);
    float prior = RemapFrame(playerModel, pri, entity->PriorAnimationFrame, bm, mappedPri, priFam);
    // MEAMU TRAIN 110: locomotion pose smoothing. BMD_Animation slerps the
    // PRIOR action/frame sample into the current one with the current
    // frame's fraction as the weight -- a contract built for samples one
    // tick apart on the SAME model. The frame remap multiplies the tick
    // delta by (beastFrames / playerFrames), so at run speed the prior
    // sample lands several beast frames behind the current one and the
    // slerp blends two distant walk phases (root translation included):
    // the form shimmers/twitches while moving. Player locomotion action
    // churn (walk/run/strafe switches, each resetting the player frame)
    // pops the cycle the same way. Fix, walk family only: the prior
    // sample is the SAME beast walk action exactly one beast frame
    // behind the current frame (wrapped) -- adjacent-sample interpolation,
    // immune to player action switches. Idle/attack/hit/die keep the
    // stock remap untouched.
    if (fam == FamWalk) {
        const int bf = ActionFrames(bm, mapped);
        if (bf > 1) {
            mappedPri = mapped;
            float p = frame - 1.0f;
            if (p < 0.0f) p += (float)bf;
            prior = p;
        }
    }

    // Contexto del modelo bestia: espejo de PrepareContext + el prólogo de
    // Calc_RenderObject (BodyHeight/ContrastEnable), con la escala NATIVA
    // de la bestia salvaje (Scale de la entidad × Scale del catálogo).
    // BMD_Animation deja las matrices sin escala cuando translate != 0; la
    // escala la aplica Skeleton_Transform desde este contexto al dibujar.
    bm->BodyScale = entity->Scale * beastScale;
    bm->CurrentAction = (unsigned char)mapped;
    std::memcpy(bm->BodyOrigin, entity->Position, sizeof(bm->BodyOrigin));
    bm->BodyHeight = 0.0f;
    bm->ContrastEnable = entity->ContrastEnable;

    DWORD priorBits;
    std::memcpy(&priorBits, &prior, sizeof(priorBits));
    float angleBuf[3] = { entity->Angle[0], entity->Angle[1], entity->Angle[2] };
    float headZero[3] = { 0.0f, 0.0f, 0.0f };   // las bestias no trackean cabeza
    BMD_Animation((void*)bm, bones, frame, priorBits, (unsigned char)mappedPri,
                  (unsigned int*)angleBuf, headZero, '\0', translate == '\0');
    return true;
}

bool DrawBody(void* entity, int translate)
{
    int modelIdx = -1;
    if (!Resolve((const CHARACTER*)entity, &modelIdx, nullptr)) return false;

    OBJECT* o = AsObject(entity);
    BMD* bm = AsModel((uintptr_t)DAT_05828d58) + modelIdx;
    // Luz del terreno sobre el modelo bestia, como Calc_RenderObject hace
    // con el modelo propio de cada monstruo (no toca BodyScale).
    Entity_GetLightScale((uintptr_t)entity, (uintptr_t)bm);
    void* bones = o->EnableBoneMatrix == 0 ? (void*)&DAT_06970a9c
                                           : (void*)o->BoneMatrices;
    Skeleton_Transform((void*)bm, (float*)bones, o->BoundingBoxMin,
                       o->BoundingBoxMax, o->OBB, (char)translate);
    // La misma llamada de cuerpo completo que recibe un monstruo salvaje
    // (LAB_simple_render de Draw_RenderObject).
    BMD__RenderBody((void*)bm, 2, o->Alpha, o->BlendMesh, o->BlendMeshLight,
                    o->BlendMeshTexCoordU, o->BlendMeshTexCoordV,
                    o->HiddenMesh, 0xffffffff);
    return true;
}

} // namespace BijuTransform
