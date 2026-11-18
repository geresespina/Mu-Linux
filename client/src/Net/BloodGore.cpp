// BloodGore.cpp — see BloodGore.h for the design. Mirrors the fork's
// module shapes: HpBars (receive-fed table + HUD-pass render) for the
// splats/flash, the engine particle pool (Particle_Spawn + post-spawn
// slot tuning) for the bursts — no new particle engine.

#include "stdafx.h"
#include "Net/BloodGore.h"
#include "Config/UserSettings.h"
#include "Core/Window.h"
#include "Entity/EntityView.h"
#include "Game/HeroVitals.h"
#include "Platform/ITimer.h"
#include "Render/BufferedDraw.h"
#include "Render/EffectPools.h"
#include "Render/Gfx.h"
#include "globals.h"
#include "functions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using namespace BloodGore;

// Particle slot layout (Particle_Spawn.cpp / Particle_Move.cpp P_*
// macros, stride 0x70 in DAT_07abf5f0). Type 259 has NO case in
// Particle_Spawn (base init only: lifetime 2), NO case in
// MoveParticles (generic path only: lifetime--, then
// pos += Rotate(dir, vel) — no colour overwrite, no gravity), and a
// dedicated UV-cell case in RenderParticles (ItemDrop_Render2.cpp).
constexpr int kSlotType     = 0x04;
constexpr int kSlotLife     = 0x38;
constexpr int kSlotRotation = 0x40;
constexpr int kSlotVelX     = 0x4C;
constexpr int kSlotVelY     = 0x50;
constexpr int kSlotVelZ     = 0x54;

constexpr int   kMaxSplats       = 32;
constexpr int   kBurstSmall      = 5;
constexpr int   kBurstMedium     = 9;
constexpr int   kBurstHeavy      = 18;
constexpr int   kBurstDeath      = 26;
constexpr uint32_t kThrottleMs   = 40;
constexpr uint32_t kDeathSplatMs = 10000;
constexpr uint32_t kHitSplatMs   = 6000;
constexpr uint32_t kFlashMs      = 220;

struct Splat {
    float x = 0, y = 0, z = 0;
    float radius = 0, rot = 0;
    uint32_t born = 0, lifeMs = 0;
    bool active = false;
};
Splat s_Splats[kMaxSplats];
int   s_SplatNext = 0;

uint32_t s_HurtFlashTick = 0;

// Per-entity burst throttle (pointer-keyed ring; entities are pool
// slots, pointers stay valid while the entity lives).
struct Throttle { const void* ent; uint32_t tick; };
Throttle s_Throttle[32];
int      s_ThrottleNext = 0;

uint32_t NowMs()
{
    return (uint32_t)Platform::GetTimer().Ticks();
}

bool Enabled()
{
    return gUserSettings.GetBloodGore();
}

// CHARACTER.SafeZone (EntityTypes.h, static-asserted at 0x34E):
// "TerrainWall & 1 en la casilla actual" — no combat happens there.
bool InSafeZone(const void* entity)
{
    return entity && ((const unsigned char*)entity)[0x34E] != 0;
}

bool Throttled(const void* entity, uint32_t now)
{
    for (const Throttle& t : s_Throttle)
        if (t.ent == entity && (now - t.tick) < kThrottleMs) return true;
    s_Throttle[s_ThrottleNext] = { entity, now };
    s_ThrottleNext = (s_ThrottleNext + 1) & 31;
    return false;
}

// Damage share of the victim's pre-hit HP. -1 when unknowable.
float DamageFraction(uint32_t damage, uint32_t viewCurHP, bool hasViewFields,
                     bool isHero)
{
    if (hasViewFields && (damage + viewCurHP) > 0)
        return (float)damage / (float)(damage + viewCurHP);
    if (isHero) {
        const DWORD max = gHeroVitals.GetMax(VITAL_LIFE);
        if (max > 0) return (float)damage / (float)max;
    }
    return -1.0f;
}

enum class Tier { Small, Medium, Heavy };
Tier TierOf(float fraction, uint32_t damage)
{
    if (fraction >= 0.0f) {
        if (fraction >= kHeavyFraction) return Tier::Heavy;
        if (fraction >= 0.03f)           return Tier::Medium;
        return Tier::Small;
    }
    // Legacy packet without the EXTRA fields: absolute tiers.
    if (damage >= 4000) return Tier::Heavy;
    if (damage >= 400)  return Tier::Medium;
    return Tier::Small;
}

void TuneSlot(int slotIndex, int lifeTicks)
{
    // Particle_Spawn returns the pool slot INDEX (every exit path in
    // Particle_Spawn.cpp returns iVar6), NOT a byte offset -- the pool
    // stride is 0x70. Gore fix 2026-11-18: the original code treated the
    // return value as a byte offset, so this tuning silently never landed
    // (the type check below read slot 0's fields and bailed) and bursts
    // kept the base-init lifetime of 2 ticks with zero velocity --
    // a ~1-frame static flash, effectively invisible in real play.
    char* slot = DAT_07abf5f0 + slotIndex * 0x70;
    if (!slot[0] || *(int*)(slot + kSlotType) != kBitmap) return;
    *(int*)(slot + kSlotLife) = lifeTicks;
    *(float*)(slot + kSlotRotation) = (float)(rand() % 360);
    // Radial spray: MoveParticles rotates vel by dir (angle vector),
    // so a +X velocity with a random Z angle sprays in every direction.
    *(float*)(slot + kSlotVelX) = 1.2f + (float)(rand() % 30) * 0.1f;
    *(float*)(slot + kSlotVelY) = 0.0f;
    *(float*)(slot + kSlotVelZ) = (float)((rand() % 13) - 5) * 0.45f;
}

void SpawnBurst(float x, float y, float z, int count, float scaleMul,
                float r, float g, float b, int lifeBase)
{
    for (int i = 0; i < count; ++i) {
        float pos[3] = { x + (float)((rand() % 21) - 10),
                         y + (float)((rand() % 21) - 10),
                         z + (float)(rand() % 25) };
        float dir[3] = { 0.0f, 0.0f, (float)(rand() % 360) };
        // Slight per-droplet variance so the spray is not one flat red.
        float vr = r * (0.85f + (float)(rand() % 31) * 0.01f);
        float color[3] = { vr, g, b };
        float scale = (0.10f + (float)(rand() % 13) * 0.01f) * scaleMul;
        const int off = Particle_Spawn(kBitmap, pos, dir, color, 0, scale, 0);
        TuneSlot(off, lifeBase + (rand() % 7));
    }
}

void BurstColor(const EntityView& view, float& r, float& g, float& b)
{
    // Wild Biju (classes 155-164) bleed dark ichor; everyone else
    // dark blood red. Same texture, tint only — costs nothing.
    if (view.Kind() == EntityView::MonsterKind) {
        const int cls = view.MonsterIndex();
        if (cls >= 155 && cls <= 164) { r = 0.30f; g = 0.02f; b = 0.11f; return; }
    }
    r = 0.50f; g = 0.04f; b = 0.04f;
}

float ChestZ(const EntityView& view)
{
    float h = view.Height() * 0.55f;
    if (h < 40.0f) h = 40.0f;
    if (h > 500.0f) h = 500.0f;
    return view.Z() + h;
}

void AddSplat(float x, float y, float radius, uint32_t lifeMs, uint32_t now)
{
    Splat& s = s_Splats[s_SplatNext];
    s_SplatNext = (s_SplatNext + 1) & (kMaxSplats - 1);
    s.x = x; s.y = y;
    s.z = RequestTerrainHeight(x, y);
    s.radius = radius;
    s.rot = (float)(rand() % 360) * 0.0174533f;
    s.born = now;
    s.lifeMs = lifeMs;
    s.active = true;
}

void RunSelfTestOnce()
{
    static bool done = false;
    if (done) return;
    done = true;
    if (!std::getenv("MU_BLOODGORE_SELFTEST")) return;

    // Fabricated entity: active, monster kind, mid-field, not safe.
    static unsigned char ent[0x394];
    std::memset(ent, 0, sizeof(ent));
    ent[0] = 1;                       // active
    ent[0x84] = EntityView::MonsterKind;
    *(float*)(ent + 0x10) = 1000.0f;  // Position xyz (raw offsets,
    *(float*)(ent + 0x14) = 1000.0f;  // the same ones Recv_Combat
    *(float*)(ent + 0x18) = 100.0f;   // reads)

    const int before = BloodGore::ActiveBurstParticles();
    BloodGore::OnHit(ent, 500, 500, true, false);   // 50% = heavy
    const int afterHit = BloodGore::ActiveBurstParticles();
    const int splatsAfterHit = BloodGore::ActiveSplats();
    BloodGore::OnHit(ent, 0, 500, true, false);     // MISS = nothing
    const int afterMiss = BloodGore::ActiveBurstParticles();
    BloodGore::OnDeath(ent);
    const int afterDeath = BloodGore::ActiveBurstParticles();
    const int splatsAfterDeath = BloodGore::ActiveSplats();

    const bool ok = afterHit >= before + kBurstHeavy
                 && splatsAfterHit == 1
                 && afterMiss == afterHit
                 && afterDeath >= afterMiss + kBurstDeath
                 && splatsAfterDeath == 2;
    std::printf("BLOODGORE SELFTEST: %s (burst %d->%d, miss %d, death %d; splats %d->%d)\n",
                ok ? "PASS" : "FAIL", before, afterHit, afterMiss,
                afterDeath, splatsAfterHit, splatsAfterDeath);
    std::fflush(stdout);
    BloodGore::ClearAll();
}

void DrawSplats(uint32_t now)
{
    const BYTE* hero = (const BYTE*)(uintptr_t)DAT_07abf5d8;
    float hx = 0, hy = 0;
    bool haveHero = false;
    if (hero && hero[0]) {
        const EntityView hv(hero);
        hx = hv.X(); hy = hv.Y(); haveHero = true;
    }

    for (const Splat& s : s_Splats) {
        if (!s.active) continue;
        const uint32_t age = now - s.born;
        if (age > s.lifeMs) continue;
        if (haveHero) {
            const float dx = s.x - hx, dy = s.y - hy;
            if (dx * dx + dy * dy > 2600.0f * 2600.0f) continue;
        }
        float alpha = 0.92f;
        if (age > s.lifeMs - 2000)
            alpha = 0.92f * (float)(s.lifeMs - age) / 2000.0f;

        // Ground quad corners (rotated), projected like the HP bars.
        const float cs = std::cos(s.rot), sn = std::sin(s.rot);
        const float lx[4] = { -1, -1, 1, 1 };
        const float ly[4] = { -1, 1, 1, -1 };
        const float uu[4] = { 0.5f, 0.5f, 1.0f, 1.0f };   // splat cell:
        const float vv[4] = { 0.25f, 0.75f, 0.75f, 0.25f }; // u .5-1, v .25-.75
        int sx[4], sy[4];
        bool okProj = true;
        for (int k = 0; k < 4; ++k) {
            float wp[3] = { s.x + (lx[k] * cs - ly[k] * sn) * s.radius,
                            s.y + (lx[k] * sn + ly[k] * cs) * s.radius,
                            s.z + 2.0f };
            Camera_ProjectWorldToScreen(wp, &sx[k], &sy[k]);
            if (sx[k] < -640 || sx[k] > 1280 || sy[k] < -480 || sy[k] > 960)
                okProj = false;
        }
        if (!okProj) continue;

        GL_BindTextureSlot(kBitmap);
        EnableAlphaTest(true);
        EnableAlphaBlend();
        Gfx::Color4f(1.0f, 1.0f, 1.0f, alpha);
        BufferedDraw::Begin(BufferedDraw::Primitive::TriangleFan);
        for (int k = 0; k < 4; ++k) {
            BufferedDraw::TexCoord2f(uu[k], vv[k]);
            BufferedDraw::Vertex2f((float)Screen_ToGLX((float)sx[k]),
                                   (float)gWindow.GetHeight() -
                                   (float)Screen_ToGLY((float)sy[k]));
        }
        BufferedDraw::End();
        Gfx::Color4f(1.0f, 1.0f, 1.0f, 1.0f);
        GL_ResetState();
    }
}

void DrawHurtFlash(uint32_t now)
{
    if (!s_HurtFlashTick) return;
    const uint32_t age = now - s_HurtFlashTick;
    if (age >= kFlashMs) { s_HurtFlashTick = 0; return; }
    const float a = (1.0f - (float)age / (float)kFlashMs) * 0.38f;

    EnableAlphaTest(true);
    EnableAlphaBlend();
    Gfx::Color4f(0.62f, 0.02f, 0.02f, a);
    GL_DrawRect(0.0f, 0.0f, 640.0f, 54.0f);          // top
    GL_DrawRect(0.0f, 480.0f - 54.0f, 640.0f, 54.0f); // bottom
    GL_DrawRect(0.0f, 0.0f, 50.0f, 480.0f);           // left
    GL_DrawRect(640.0f - 50.0f, 0.0f, 50.0f, 480.0f); // right
    Gfx::Color4f(0.62f, 0.02f, 0.02f, a * 0.45f);
    GL_DrawRect(0.0f, 54.0f, 640.0f, 26.0f);
    GL_DrawRect(0.0f, 480.0f - 80.0f, 640.0f, 26.0f);
    GL_DrawRect(50.0f, 0.0f, 24.0f, 480.0f);
    GL_DrawRect(640.0f - 74.0f, 0.0f, 24.0f, 480.0f);
    Gfx::Color4f(1.0f, 1.0f, 1.0f, 1.0f);
    GL_ResetState();
}

// Gore fix 2026-11-18: env-gated VISUAL test (MU_BLOODGORE_VISUALTEST=1).
// Every 2.5 s, fabricates a heavy hit + a death on a copy of the hero at
// the hero's live position, so bursts + splats can be verified on a real
// renderer without combat. Inert unless the env var is set.
void VisualTestTick(uint32_t now)
{
    static const bool enabled = std::getenv("MU_BLOODGORE_VISUALTEST") != nullptr;
    if (!enabled) return;
    static uint32_t last = 0;
    if (now - last < 2500) return;
    const BYTE* hero = (const BYTE*)(uintptr_t)DAT_07abf5d8;
    if (!hero || !hero[0]) return;
    last = now;
    static unsigned char ent[0x394];
    std::memcpy(ent, hero, sizeof(ent));
    ent[0x34E] = 0;  // fabricated copy: allow effects even on safe tiles
    BloodGore::OnHit(ent, 500, 500, true, false);   // 50% = heavy burst + splat
    BloodGore::OnDeath(ent);                        // death burst + splat
    std::printf("BLOODGORE VISUALTEST: burst+splat at hero pos\n");
    std::fflush(stdout);
}

} // namespace

namespace BloodGore {

void OnHit(const void* entity, uint32_t damage, uint32_t viewCurHP,
           bool hasViewFields, bool isHero)
{
    if (!Enabled() || !entity || damage == 0) return;   // MISS = nothing
    if (InSafeZone(entity)) return;
    const uint32_t now = NowMs();
    if (Throttled(entity, now)) return;

    const EntityView view(entity);
    if (!view.IsActive()) return;

    const float fraction = DamageFraction(damage, viewCurHP, hasViewFields, isHero);
    const Tier tier = TierOf(fraction, damage);

    float r, g, b;
    BurstColor(view, r, g, b);
    const int count = tier == Tier::Heavy  ? kBurstHeavy
                    : tier == Tier::Medium ? kBurstMedium : kBurstSmall;
    const float scaleMul = tier == Tier::Heavy ? 1.5f : tier == Tier::Medium ? 1.15f : 0.8f;
    SpawnBurst(view.X(), view.Y(), ChestZ(view), count, scaleMul, r, g, b, 12);

    if (tier == Tier::Heavy) {
        AddSplat(view.X(), view.Y(), 34.0f + (float)(rand() % 14), kHitSplatMs, now);
        if (isHero) s_HurtFlashTick = now;
    }
}

void OnDeath(const void* entity)
{
    if (!Enabled() || !entity) return;
    if (InSafeZone(entity)) return;
    const EntityView view(entity);
    if (!view.IsActive()) return;
    const uint32_t now = NowMs();

    float r, g, b;
    BurstColor(view, r, g, b);
    SpawnBurst(view.X(), view.Y(), ChestZ(view), kBurstDeath, 2.0f, r, g, b, 16);
    AddSplat(view.X(), view.Y(), 55.0f + (float)(rand() % 20), kDeathSplatMs, now);
}

int ActiveBurstParticles()
{
    int n = 0;
    for (int i = 0; i < kParticlePoolSize; ++i) {
        const char* slot = DAT_07abf5f0 + i * 0x70;
        if (slot[0] && *(const int*)(slot + kSlotType) == kBitmap) ++n;
    }
    return n;
}

int ActiveSplats()
{
    const uint32_t now = NowMs();
    int n = 0;
    for (const Splat& s : s_Splats)
        if (s.active && (now - s.born) <= s.lifeMs) ++n;
    return n;
}

void ClearAll()
{
    for (int i = 0; i < kParticlePoolSize; ++i) {
        char* slot = DAT_07abf5f0 + i * 0x70;
        if (slot[0] && *(int*)(slot + kSlotType) == kBitmap) slot[0] = 0;
    }
    for (Splat& s : s_Splats) s.active = false;
    s_HurtFlashTick = 0;
}

} // namespace BloodGore

void Render_BloodGore(void)
{
    RunSelfTestOnce();
    if (!Enabled()) return;
    if (SceneFlag != 5) return;
    if (!CharactersClient) return;
    const uint32_t now = NowMs();
    DrawSplats(now);
    DrawHurtFlash(now);
    VisualTestTick(now);
}
