#pragma once
// BloodGore.h — MEAMU Blood & Gore (client only, G ask 2026-11-17 22:42).
//
// Three effects, all fed by the combat receive handlers the client
// already runs (the same damage flow the HP-bars build rides):
//
//   1. HIT BURSTS  — NetRecv_15 (0x15 GCDamageSend) calls OnHit for
//      every landed hit (damage > 0; a MISS packet carries damage 0
//      and spawns nothing). A burst of dark-red droplet particles
//      (engine particle pool, type = BITMAP_BLOODGORE) at the victim's
//      chest. Size scales with the damage fraction of the victim's
//      HP: <3% a few flecks, 3-10% a spray, >=10% a heavy spray.
//   2. DEATH SPLAT — NetRecv_17 (player die) and NetRecv_9C (monster
//      die / PvP die) call OnDeath: a larger burst plus a blood splat
//      decal pooled on the ground under the corpse (~10 s, fading).
//      Victims of >=10% hits also leave a smaller splat (~6 s).
//   3. HERO HURT FLASH — a heavy hit on the hero arms a ~200 ms red
//      edge-vignette pulse, drawn by Render_BloodGore in the HUD pass
//      (hooked in Render_GameFrame right after Render_FloatingText,
//      the same slot the HP-bars pass uses).
//
// Toggle: Config.ini [Game] BloodGore 1/0 (default 1) via
// gUserSettings.GetBloodGore(). Off = zero behaviour change: the
// receive hooks early-out and the render pass draws nothing.
//
// Textures: ONE atlas, bitmap slot 259 (0x103) — see BloodGore.cpp
// for the slot audit. Data/Interface/BloodGore.ozt (TGA: JPEG/.ozj
// has no alpha channel and a splat needs real alpha), left half =
// droplet sprite (particle UV cell), right half = ground splat.
//
// Perf caps: <=18 particles per hit burst, <=26 per death burst,
// per-entity burst throttle 40 ms, particle lifetime <=18 ticks,
// splat pool 32 (ring, oldest recycled). Worst case ~20 simultaneous
// heavy bursts = ~470 of the pool's 6000 slots, all dead in ~0.3 s.
//
// Self-test: MU_BLOODGORE_SELFTEST=1 runs a one-shot fabrication at
// first render (BoostStrip/HealthBar pattern) proving hit events
// spawn bursts and deaths spawn splats; prints BLOODGORE SELFTEST
// PASS/FAIL to stdout. No-op without the env var.

#include <cstdint>

namespace BloodGore {

// Bitmap slot AND particle type for the whole pack (one atlas).
constexpr int kBitmap = 259;              // 0x103
constexpr float kHeavyFraction = 0.10f;   // >=10% of victim HP = heavy

// Called from NetRecv_15 for BOTH branches (stun + main) once the
// target entity is resolved. viewCurHP/hasViewFields carry the
// packet's GAMESERVER_EXTRA fields (post-hit HP of the target) used
// for the exact damage fraction; without them the hero falls back to
// gHeroVitals and other victims to absolute-damage tiers.
void OnHit(const void* entity, uint32_t damage, uint32_t viewCurHP,
           bool hasViewFields, bool isHero);

// Called from NetRecv_17 / NetRecv_9C with the dying entity.
void OnDeath(const void* entity);

// Test/diagnostic surface (also used by the env-gated self-test).
int ActiveBurstParticles();
int ActiveSplats();
void ClearAll();

} // namespace BloodGore

// HUD-pass render: decals + hero hurt vignette. Declared free (not in
// the namespace) to match Render_HpBars / Render_FloatingText.
void Render_BloodGore(void);
