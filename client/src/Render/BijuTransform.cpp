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
#include "Entity/EntityTypes.h"
#include "Item/ContentCatalog.h"
#include "globals.h"
#include "functions.h"
#include <cmath>
#include <cstring>

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

    signed char s_beastBySlot[400];
    bool        s_init = false;

    void EnsureInit()
    {
        if (!s_init) {
            std::memset(s_beastBySlot, -1, sizeof(s_beastBySlot));
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
        if ((*(const DWORD*)((const BYTE*)character + 120) & 0x2000) == 0)
            return false;
        EnsureInit();
        const int slot = SlotOf(character);
        if (slot < 0) return false;
        const int beast = s_beastBySlot[slot];
        if (beast < 0 || beast >= kBeastCount) return false;
        const CatalogMonster* cm = gContentCatalog.GetMonster(kFirstBeastClass + beast);
        if (!cm || cm->Model < 0) return false;
        const BMD* bm = AsModel((uintptr_t)DAT_05828d58) + cm->Model;
        if (bm->NumActions <= 0 || bm->NumMeshs <= 0 || !bm->Actions || !bm->Bones)
            return false;   // modelo no cargado → render normal del jugador
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
    if (slot >= 0) s_beastBySlot[slot] = -1;
}

int BeastFor(const CHARACTER* character)
{
    EnsureInit();
    const int slot = SlotOf(character);
    return (slot >= 0) ? (int)s_beastBySlot[slot] : -1;
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
    const int mappedPri = BeastAction(bm, priFam);
    const float frame = RemapFrame(playerModel, cur, entity->AnimationFrame, bm, mapped, fam);
    const float prior = RemapFrame(playerModel, pri, entity->PriorAnimationFrame, bm, mappedPri, priFam);

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
