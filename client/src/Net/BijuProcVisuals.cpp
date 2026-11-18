// BijuProcVisuals.cpp -- MEAMU Biju Powers proc visuals (Train 2,
// Module B, G approved 2026-11-18: "real passive visuals, per-beast,
// not generic stock reuse").
//
// DESIGN
//   The passives proc on the GS (CustomBijuPowers -- it owns the
//   rolls), so the GS is the source of the visual moment: it emits a
//   small custom packet at each proc, and this module turns it into a
//   beast-coloured particle recipe at the right entity. Two packet
//   shapes share C1 F3 F9 (10 bytes):
//
//     kind 0  BEARER STATE: [5]=tails, [6..7]=bearer key,
//             [8]=aux (Ten-Tails: orbs left; Kokuo: steam stacks).
//             Sent on equip change, orb eat/regrow, steam change and
//             on a 10 s refresh. Drives the CONTINUOUS visuals that
//             only the client can draw: orbiting Truth-Seeker Orbs
//             and Steam Pressure puffs.
//     kind>0  PROC: [5]=tails, [6..7]=bearer key, [8..9]=target key.
//             One-shot bursts (see ProcId). Ignite / Acid Apply also
//             arm a 3 s ember overlay on the victim so the burn reads
//             for its whole duration, not one frame.
//
//   Effects ride the engine particle pool exactly like BloodGore:
//   Particle_Spawn (base init only for our type) + post-spawn slot
//   tuning. ONE soft-disc sprite (Interface\BijuProc.ozt, TGA for the
//   alpha) tinted per beast from G's JinchurikiTint table -- the
//   recipe (count, spread, motion, placement) is what differs per
//   passive, not a borrowed stock skill animation.
//
// BITMAP AUDIT (slot 0x100 = 256, Train 6 standard: no load, no draw,
//   outside every allocator's march)
//   Loads: the interface sequence runs ...0xFE back1, 0xFF back2,
//   [0x100 FREE], 0x101 Menu03_new_AG, 0x102 BijuPowers, 0x103
//   BloodGore, 0x104.. Item_*. No OpenJPG/OpenTGA/OpenTexture anywhere
//   targets 0x100/256. Draws: no GL_DrawTexture(256/0x100. Particles:
//   no case 256 in Particle_Spawn / MoveParticles / RenderParticles
//   (the default full-texture UV case renders it). Allocators:
//   BITMAP_SKIN marches 301..307, equipment base is 310, the
//   ContentCatalog region starts at 1450 -- 256 is below all three.
//   Final proof is in-game, as with every MEAMU slot (308/309/262/259
//   were verified the same way).
//
// Self-test: MU_BIJUPROC_SELFTEST=1 fabricates a bearer-state + a
// swipe proc on the hero at first render and prints PASS/FAIL.
// MU_BIJUPROC_VISUALTEST=1 re-fires state + a rotating proc at the
// hero every 2.5 s for on-renderer verification (BloodGore pattern).

#include "stdafx.h"
#include "Net/BijuProcVisuals.h"
#include "Entity/EntityView.h"
#include "Net/Recv/NetRecv.h"
#include "Platform/ITimer.h"
#include "globals.h"
#include "functions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using namespace BijuProcVisuals;

// Particle slot layout -- identical constants to BloodGore.cpp
// (Particle_Spawn.cpp / Particle_Move.cpp P_* macros, stride 0x70 in
// DAT_07abf5f0). Type 256 has NO case in Particle_Spawn (base init:
// lifetime 2), NO case in MoveParticles (generic path: lifetime--,
// pos += Rotate(dir, vel), no colour overwrite), and the DEFAULT
// full-texture case in RenderParticles -- exactly the 259 contract,
// minus the UV cell (our sprite fills the whole texture).
constexpr int kSlotType     = 0x04;
constexpr int kSlotLife     = 0x38;
constexpr int kSlotRotation = 0x40;
constexpr int kSlotVelX     = 0x4C;
constexpr int kSlotVelY     = 0x50;
constexpr int kSlotVelZ     = 0x54;

// Proc ids on the wire (mirror CustomBijuPowers GS side).
enum ProcId : uint8_t {
    ProcState        = 0,
    ProcShieldAbsorb = 1,
    ProcShieldRegrow = 2,
    ProcIgnite       = 3,
    ProcCoralSting   = 4,
    ProcLavaSplash   = 5,
    ProcAcidApply    = 6,
    ProcBlindApply   = 7,
    ProcGripBind     = 8,
    ProcInkCloneSave = 9,
    ProcChakraSwipe  = 10,
    ProcOrbEat       = 11,
    ProcDotTick      = 12,
};

// G's JinchurikiTint table (Render/BijuTransform.cpp), tails 1..10.
const float kBeastColor[11][3] = {
    { 0.0f, 0.0f, 0.0f },        // 0 = none
    { 0.85f, 0.64f, 0.25f },     // 1 Shukaku   #D9A441
    { 0.24f, 0.48f, 0.98f },     // 2 Matatabi  #3E7BFA
    { 0.18f, 0.70f, 0.65f },     // 3 Isobu     #2FB3A6
    { 0.89f, 0.28f, 0.24f },     // 4 Son Goku  #E2483D
    { 0.93f, 0.94f, 0.95f },     // 5 Kokuo     #EDEFF2
    { 0.95f, 0.65f, 0.76f },     // 6 Saiken    #F2A7C3
    { 0.48f, 0.78f, 0.31f },     // 7 Chomei    #7AC74F
    { 0.56f, 0.42f, 0.78f },     // 8 Gyuki     #8E6CC8
    { 0.96f, 0.52f, 0.12f },     // 9 Kurama    #F5851F
    { 0.73f, 0.77f, 0.69f },     // 10 Ten-Tails #B9C4B1
};
// Acid reads green whatever the beast tint (Acid Body / weak acid).
const float kAcidColor[3] = { 0.45f, 0.90f, 0.30f };

struct BearerState {
    uint16_t key = 0;
    uint8_t  tails = 0;
    uint8_t  aux = 0;        // tails 10: orbs left; tails 5: steam stacks
};
BearerState s_Bearers[48];

struct Ember {
    uint16_t key = 0;
    uint8_t  tails = 0;      // 2 = blue fire, 6 = acid
    uint32_t untilMs = 0;
};
Ember s_Embers[16];

struct Throttle { const void* ent; uint32_t tick; };
Throttle s_Throttle[32];
int      s_ThrottleNext = 0;

uint32_t NowMs()
{
    return (uint32_t)Platform::GetTimer().Ticks();
}

const void* ResolveEntity(int key)
{
    if (key <= 0 || !CharactersClient) return nullptr;
    const int slot = Entity_FindById(key);
    if (slot >= 400) return nullptr;
    return (const void*)((BYTE*)(uintptr_t)CharactersClient + slot * CHARACTER_STRIDE);
}

float ChestZ(const EntityView& view)
{
    float h = view.Height() * 0.55f;
    if (h < 40.0f) h = 40.0f;
    if (h > 500.0f) h = 500.0f;
    return view.Z() + h;
}

void TuneSlot(int slotIndex, int lifeTicks, float velX, float velZ)
{
    // Same contract as BloodGore::TuneSlot: Particle_Spawn returns the
    // pool slot INDEX; the stride is 0x70.
    char* slot = DAT_07abf5f0 + slotIndex * 0x70;
    if (!slot[0] || *(int*)(slot + kSlotType) != kBitmap) return;
    *(int*)(slot + kSlotLife) = lifeTicks;
    *(float*)(slot + kSlotRotation) = (float)(rand() % 360);
    *(float*)(slot + kSlotVelX) = velX;
    *(float*)(slot + kSlotVelY) = 0.0f;
    *(float*)(slot + kSlotVelZ) = velZ;
}

void SpawnDisc(float x, float y, float z, const float color[3],
               float scale, int lifeTicks, float velX, float velZ)
{
    float pos[3] = { x, y, z };
    float dir[3] = { 0.0f, 0.0f, (float)(rand() % 360) };
    float col[3] = { color[0], color[1], color[2] };
    const int slot = Particle_Spawn(kBitmap, pos, dir, col, 0, scale, 0);
    TuneSlot(slot, lifeTicks, velX, velZ);
}

void Burst(const EntityView& view, const float color[3], int count,
           float scaleMul, int lifeBase, bool headHeight)
{
    const float z = headHeight ? view.Z() + view.Height() * 0.85f
                               : ChestZ(view);
    for (int i = 0; i < count; ++i) {
        const float px = view.X() + (float)((rand() % 41) - 20);
        const float py = view.Y() + (float)((rand() % 41) - 20);
        const float pz = z + (float)(rand() % 25);
        const float scale = (0.12f + (float)(rand() % 11) * 0.015f) * scaleMul;
        SpawnDisc(px, py, pz, color, scale, lifeBase + (rand() % 6),
                  1.0f + (float)(rand() % 25) * 0.08f,
                  (float)((rand() % 11) - 4) * 0.4f);
    }
}

bool Throttled(const void* entity, uint32_t now)
{
    for (const Throttle& t : s_Throttle)
        if (t.ent == entity && (now - t.tick) < 40) return true;
    s_Throttle[s_ThrottleNext] = { entity, now };
    s_ThrottleNext = (s_ThrottleNext + 1) & 31;
    return false;
}

void ArmEmber(uint16_t key, uint8_t tails, uint32_t now)
{
    Ember* slot = nullptr;
    for (Ember& e : s_Embers) if (e.key == key && e.tails == tails) { slot = &e; break; }
    if (!slot) for (Ember& e : s_Embers) if (now >= e.untilMs) { slot = &e; break; }
    if (!slot) slot = &s_Embers[0];
    slot->key = key;
    slot->tails = tails;
    slot->untilMs = now + 3000;   // both dots run 3 s (3 ticks)
}

void UpsertBearer(uint16_t key, uint8_t tails, uint8_t aux)
{
    BearerState* freeSlot = nullptr;
    for (BearerState& b : s_Bearers) {
        if (b.key == key) {
            b.tails = tails;
            b.aux = aux;
            if (!tails) b.key = 0;
            return;
        }
        if (!b.key && !freeSlot) freeSlot = &b;
    }
    if (!freeSlot || !tails) return;
    freeSlot->key = key;
    freeSlot->tails = tails;
    freeSlot->aux = aux;
}

void OnProc(uint8_t proc, uint8_t tails, int targetKey, uint32_t now)
{
    const void* ent = ResolveEntity(targetKey);
    if (!ent) return;
    const EntityView view(ent);
    if (!view.IsActive()) return;
    if (Throttled(ent, now)) return;
    const float* beast = kBeastColor[tails <= 10 ? tails : 0];

    switch (proc) {
        case ProcShieldAbsorb:   // sand-gold shell eats the hit
            Burst(view, beast, 8, 1.0f, 9, false);
            break;
        case ProcShieldRegrow:   // soft gold rise as the shield regrows
            Burst(view, beast, 8, 0.9f, 12, false);
            break;
        case ProcIgnite:         // blue fire catches + keeps burning
            Burst(view, beast, 12, 1.1f, 11, false);
            ArmEmber((uint16_t)targetKey, 2, now);
            break;
        case ProcDotTick:        // per-second dot pulse (fire or acid)
            Burst(view, tails == 6 ? kAcidColor : beast, 4, 0.8f, 8, false);
            break;
        case ProcCoralSting:     // teal sting back at the attacker
            Burst(view, beast, 10, 1.0f, 10, false);
            break;
        case ProcLavaSplash:     // lava splash on each splashed enemy
            Burst(view, beast, 10, 1.05f, 10, false);
            break;
        case ProcAcidApply:      // acid coats the victim, keeps eating
            Burst(view, kAcidColor, 10, 1.0f, 10, false);
            ArmEmber((uint16_t)targetKey, 6, now);
            break;
        case ProcBlindApply:     // scale powder bursts at the eyes
            Burst(view, beast, 12, 0.9f, 10, true);
            break;
        case ProcGripBind: {     // violet ring closes at the feet
            const float z = view.Z() + 12.0f;
            for (int i = 0; i < 8; ++i) {
                const float a = (float)i * 0.7853981f;
                SpawnDisc(view.X() + std::cos(a) * 42.0f,
                          view.Y() + std::sin(a) * 42.0f, z, beast,
                          0.16f, 14, 0.2f, 0.1f);
            }
            Burst(view, beast, 4, 0.8f, 10, false);
            break;
        }
        case ProcInkCloneSave:   // the substitution bursts in ink violet
            Burst(view, beast, 16, 1.4f, 13, false);
            break;
        case ProcChakraSwipe:    // the chakra arm lands on the 2nd enemy
            Burst(view, beast, 12, 1.25f, 11, false);
            break;
        case ProcOrbEat:         // an orb collapses eating the hit
            Burst(view, beast, 6, 1.1f, 9, false);
            break;
        default:
            break;
    }
}

void RunSelfTestOnce()
{
    static bool done = false;
    if (done) return;
    done = true;
    if (!std::getenv("MU_BIJUPROC_SELFTEST")) return;

    const BYTE* hero = (const BYTE*)(uintptr_t)DAT_07abf5d8;
    if (!hero || !hero[0]) { std::printf("BIJUPROC SELFTEST: SKIP (no hero)\n"); return; }
    const EntityView hv(hero);
    const uint16_t key = hv.NetworkKey();

    // Fabricated wire packets through the real Receive path.
    uint8_t state[10] = { 0xC1, 10, 0xF3, 0xF9, 0, 10,
                          (uint8_t)(key & 0xFF), (uint8_t)(key >> 8), 3, 0 };
    uint8_t proc[10]  = { 0xC1, 10, 0xF3, 0xF9, ProcChakraSwipe, 9,
                          (uint8_t)(key & 0xFF), (uint8_t)(key >> 8),
                          (uint8_t)(key & 0xFF), (uint8_t)(key >> 8) };
    const int bearersBefore = BijuProcVisuals::BearerStateCount();
    BijuProcVisuals::Receive(state, 10);
    const int bearersAfter = BijuProcVisuals::BearerStateCount();
    const int partsBefore = BijuProcVisuals::ActiveParticles();
    BijuProcVisuals::Receive(proc, 10);
    const int partsAfter = BijuProcVisuals::ActiveParticles();

    const bool ok = bearersAfter == bearersBefore + 1 && partsAfter > partsBefore;
    std::printf("BIJUPROC SELFTEST: %s (bearers %d->%d, particles %d->%d)\n",
                ok ? "PASS" : "FAIL", bearersBefore, bearersAfter,
                partsBefore, partsAfter);
    std::fflush(stdout);
    BijuProcVisuals::ClearAll();
}

void VisualTestTick(uint32_t now)
{
    static const bool enabled = std::getenv("MU_BIJUPROC_VISUALTEST") != nullptr;
    if (!enabled) return;
    static uint32_t last = 0;
    if (now - last < 2500) return;
    const BYTE* hero = (const BYTE*)(uintptr_t)DAT_07abf5d8;
    if (!hero || !hero[0]) return;
    last = now;
    const EntityView hv(hero);
    const uint16_t key = hv.NetworkKey();
    uint8_t state[10] = { 0xC1, 10, 0xF3, 0xF9, 0, 10,
                          (uint8_t)(key & 0xFF), (uint8_t)(key >> 8), 3, 0 };
    BijuProcVisuals::Receive(state, 10);
    static const uint8_t kCycle[6][2] = {
        { ProcChakraSwipe, 9 }, { ProcIgnite, 2 }, { ProcShieldAbsorb, 1 },
        { ProcGripBind, 8 },    { ProcInkCloneSave, 8 }, { ProcOrbEat, 10 },
    };
    static int step = 0;
    uint8_t proc[10] = { 0xC1, 10, 0xF3, 0xF9, kCycle[step % 6][0], kCycle[step % 6][1],
                         (uint8_t)(key & 0xFF), (uint8_t)(key >> 8),
                         (uint8_t)(key & 0xFF), (uint8_t)(key >> 8) };
    ++step;
    BijuProcVisuals::Receive(proc, 10);
    std::printf("BIJUPROC VISUALTEST: state(orbs=3) + proc %d at hero\n", proc[4]);
    std::fflush(stdout);
}

} // namespace

namespace BijuProcVisuals {

void Receive(const uint8_t* msg, int size)
{
    if (!msg || size < 10) return;
    if (msg[0] != 0xC1 || msg[2] != 0xF3 || msg[3] != 0xF9) return;
    const uint8_t kind = msg[4];
    const uint8_t tails = msg[5];
    const int ownerKey = msg[6] | (msg[7] << 8);
    if (kind == ProcState) {
        UpsertBearer((uint16_t)ownerKey, tails, msg[8]);
        return;
    }
    const int targetKey = msg[8] | (msg[9] << 8);
    OnProc(kind, tails, targetKey, NowMs());
}

int BearerStateCount()
{
    int n = 0;
    for (const BearerState& b : s_Bearers) if (b.key && b.tails) ++n;
    return n;
}

int ActiveParticles()
{
    int n = 0;
    for (int i = 0; i < kParticlePoolSize; ++i) {
        const char* slot = DAT_07abf5f0 + i * 0x70;
        if (slot[0] && *(const int*)(slot + kSlotType) == kBitmap) ++n;
    }
    return n;
}

void ClearAll()
{
    for (BearerState& b : s_Bearers) b = BearerState{};
    for (Ember& e : s_Embers) e = Ember{};
    for (int i = 0; i < kParticlePoolSize; ++i) {
        char* slot = DAT_07abf5f0 + i * 0x70;
        if (slot[0] && *(int*)(slot + kSlotType) == kBitmap) slot[0] = 0;
    }
}

} // namespace BijuProcVisuals

void Render_BijuProcVisuals(void)
{
    RunSelfTestOnce();
    if (SceneFlag != 5 || !CharactersClient) return;
    const uint32_t now = NowMs();
    VisualTestTick(now);

    // Continuous visuals from the bearer-state table.
    for (const BearerState& b : s_Bearers) {
        if (!b.key || !b.tails) continue;
        const void* ent = ResolveEntity(b.key);
        if (!ent) continue;
        const EntityView view(ent);
        if (!view.IsActive()) continue;

        if (b.tails == 10 && b.aux > 0) {
            // Truth-Seeker Orbs: one disc per orb per frame at the
            // orbit position (life 2 = the base particle lifetime, so
            // last frame's discs are already gone -- a smooth ring).
            const float t = (float)now * 0.004f;
            const float z = ChestZ(view);
            for (int i = 0; i < b.aux && i < 3; ++i) {
                const float a = t + (float)i * (6.2831853f / (float)(b.aux ? b.aux : 1));
                SpawnDisc(view.X() + std::cos(a) * 55.0f,
                          view.Y() + std::sin(a) * 55.0f,
                          z + std::sin(t * 1.7f + (float)i) * 6.0f,
                          kBeastColor[10], 0.17f, 2, 0.0f, 0.0f);
            }
        } else if (b.tails == 5 && b.aux > 0) {
            // Steam Pressure: white puffs, bigger as stacks grow.
            const float scale = 0.10f + 0.016f * (float)b.aux;
            SpawnDisc(view.X() + (float)((rand() % 31) - 15),
                      view.Y() + (float)((rand() % 31) - 15),
                      ChestZ(view), kBeastColor[5], scale, 10, 0.3f, 1.4f);
        }
    }

    // Ignite / acid embers for the dot's 3 s.
    for (const Ember& e : s_Embers) {
        if (!e.key || now >= e.untilMs) continue;
        const void* ent = ResolveEntity(e.key);
        if (!ent) continue;
        const EntityView view(ent);
        if (!view.IsActive()) continue;
        const float* color = e.tails == 6 ? kAcidColor : kBeastColor[2];
        for (int i = 0; i < 2; ++i) {
            SpawnDisc(view.X() + (float)((rand() % 45) - 22),
                      view.Y() + (float)((rand() % 45) - 22),
                      ChestZ(view) + (float)(rand() % 30), color,
                      0.11f, 8, 0.5f, 0.9f);
        }
    }
}
