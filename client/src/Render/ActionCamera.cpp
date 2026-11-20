#include "stdafx.h"
#include "Render/ActionCamera.h"
#include "Render/Camera3D.h"
#include "Render/Gfx.h"
#include "Config/UserSettings.h"
#include "Input/ActionMove.h"
#include "Input/TargetLock.h"   // Train 48: lock-driven facing   // Train 15: scheme state owner (ActionMove_IsAction)
#include "Platform/IInput.h"
#include "Platform/IWindow.h"
#include "Entity/EntityTypes.h"
#include "Net/Recv/FabricatedMark.h"
#include "UI/ChatLogWindow.h"
#include "UI/CommandMenu.h"
#include "UI/MoveList.h"
#include "UI/EventTimer.h"
#include "globals.h"
#include "functions.h"

CActionCamera gActionCamera;

// ── Tuning (MEAMU Train 14) ─────────────────────────────────────────────
// Follow distance is the 3D camera-to-aim distance at zoom factor 1.0;
// the stock wheel zoom (gCamera3D distance zoom, 0.40..2.00) scales it,
// so the effective range is 248..1240 world units. For reference the
// stock camera sits ~1312 units from the hero (1000 horizontal + 850 up).
namespace {
    const float kFollowDistance = 620.0f;
    const float kAimHeight      = 80.0f;    // aim point above the hero origin
                                            // (Train 16: 110 -> 80; framing math
                                            // in CAMFIX-REPORT — the hero now sits
                                            // in the lower third across the band)
    const float kShoulderRight  = 55.0f;    // GTA-style right-shoulder offset
    const float kPitchMin       = 6.0f;     // camera elevation band, degrees.
    const float kPitchMax       = 45.0f;    // m_Pitch is the TRUE elevation of
                                            // the camera above the aim point —
                                            // and, since Train 18, also the true
                                            // view-axis depression (see the
                                            // ApplyCamera note: CameraAngle[0]
                                            // is measured from the vertical in
                                            // this engine). Band 6..45 =
                                            // over-shoulder flat .. steep-but-
                                            // never-top-down. (Train 16's 4..38
                                            // nominal band really delivered
                                            // 86..52 degrees of depression.)
    const float kYawSens        = 0.22f;    // degrees per mouse pixel
    const float kPitchSens      = 0.18f;
    const float kFacingEase     = 0.30f;    // hero-facing ease per step
                                            // (Train 22: follow ease +
                                            // look grace removed with the
                                            // camera-follow branch)
    const float kTerrainMargin  = 45.0f;    // camera floor above the terrain
    const int   kReticleRadiusPx  = 110;    // logical px from screen centre
    const int   kReticleRangeTiles = 14;    // Chebyshev tiles from the hero

    float NormalizeDeg(float a)
    {
        while (a > 180.0f) a -= 360.0f;
        while (a <= -180.0f) a += 360.0f;
        return a;
    }
    // Signed shortest arc from `from` to `to`, in (-180, 180].
    float ShortestArc(float from, float to) { return NormalizeDeg(to - from); }

    CHARACTER* HeroC() { return (CHARACTER*)(uintptr_t)Hero; }
    CHARACTER* EntryAt(int idx)
    {
        return (CHARACTER*)((char*)(uintptr_t)CharactersClient + idx * CHARACTER_STRIDE);
    }
    int Cheb(CHARACTER* a, CHARACTER* b)
    {
        const int dx = std::abs(a->PositionX - b->PositionX);
        const int dy = std::abs(a->PositionY - b->PositionY);
        return dx > dy ? dx : dy;
    }

    // Latch legality, mirrored from KbPlay.cpp (MEAMU Trains 9/10 + Bounce
    // 11). KbPlay keeps its copies in an anonymous namespace, so they
    // cannot be linked from here; keep this predicate in lockstep with
    // KbPlay's ValidMonster / ValidLatchTarget.
    bool ValidMonster(int idx)
    {
        if (idx < 0 || idx >= 400 || !Hero) return false;
        CHARACTER* c = EntryAt(idx);
        if ((char*)c == (char*)(uintptr_t)Hero) return false;
        if (c->Object.Live == 0) return false;
        if (c->Object.Kind != 2 && c->Object.Kind != 8 && c->Object.Kind != 0x22) return false;   // Train 68: mirror of the KbPlay fix (factory writes 2/8, never 0x22)
        if (c->Dead != 0) return false;
        if (c->MonsterIndex == 0xFF) return false;
        return true;
    }
    bool ValidReticleTarget(int idx)
    {
        if (ValidMonster(idx)) return true;
        if (idx < 0 || idx >= 400 || !Hero) return false;
        CHARACTER* c = EntryAt(idx);
        if ((char*)c == (char*)(uintptr_t)Hero) return false;
        if (c->Object.Live == 0) return false;
        if (c->Dead != 0) return false;
        if (!FabricatedMark::Is(c)) return false;
        if (FabricatedMark::Kind(c) == FabricatedMark::kFollower &&
            FabricatedMark::Owner(c) == (int)HeroC()->Key) return false;
        return true;
    }

    int ScreenCentreX()
    {
        return gWindow.UiXFromPixel(gWindow.GetRealWidth() / 2);
    }
}

// ── Scheme seam ─────────────────────────────────────────────────────────

bool CActionCamera::FeatureEnabled() const
{
    return gUserSettings.GetActionCamera();
}

bool CActionCamera::Engaged() const
{
    return FeatureEnabled() && m_SchemeAction && SceneFlag == 5 && Hero != 0;
}

void CActionCamera::SetSchemeAction(bool on)
{
    if (on == m_SchemeAction) return;
    m_SchemeAction = on;
    // Flipping to Classic releases the cursor the same frame: this call
    // is synchronous with the toggle, no sim step in between.
    if (!on) ReleaseLock();
}

// ── Pointer lock ────────────────────────────────────────────────────────

void CActionCamera::EngageLock()
{
    Platform::GetInput().SetRelativeMouseMode(true);
    m_Locked = true;
    if (!m_HasYaw && Hero) {
        // Start behind the hero: camera forward == hero facing means
        // CameraAngle[2] = 180 - hero Angle[2] (see header notes).
        m_CamYaw = NormalizeDeg(180.0f - HeroC()->Object.Angle[2]);
        m_HasYaw = true;
    }
    if (Hero) {
        m_LastHeroX = HeroC()->Object.Position[0];
        m_LastHeroY = HeroC()->Object.Position[1];
    }
}

void CActionCamera::ReleaseLock()
{
    if (!m_Locked) return;
    Platform::GetInput().SetRelativeMouseMode(false);
    m_Locked = false;
}

bool CActionCamera::CursorUiOpen()
{
    // The classic panel flags (globals.h macros over DAT_07eaa11x), the
    // NPC/event dialog, any text input (chat included), the KbPlay modal
    // gate, and the clickable C++ windows. While any of these owns the
    // screen the player needs a cursor, so the lock yields.
    if (InventoryOpened || CharacterOpened || ShopOpened || WarehouseOpened ||
        ChaosMixOpened || TradeOpened || GuildOpened || PartyOpened ||
        EventWindowOpened || GuildCreatorOpened)
        return true;
    if (GoldenArcherOpenType != 0) return true;   // NPC event dialog
    if (InputEnable != 0) return true;            // chat / text input
    if (DAT_083a7c24 != 0) return true;           // modal gate (KbPlay Gates)
    if (gChatLogWindow.IsOpen() || gCommandMenu.IsOpen() ||
        gMoveList.IsOpen() || gEventTimer.IsOpen())
        return true;
    return false;
}

// ── Input ───────────────────────────────────────────────────────────────

void CActionCamera::OnMouseDelta(int dx, int dy)
{
    if (!m_Locked) return;
    if (dx == 0 && dy == 0) return;
    m_CamYaw = NormalizeDeg(m_CamYaw + (float)dx * kYawSens);
    // Pitch response (Train 18 — read with the ApplyCamera note below):
    // m_Pitch is the view-axis depression = camera elevation above the
    // aim point. "Look up" (mouse up, dy < 0) must DECREASE the
    // depression so the sightline rises toward the horizon; mouse down
    // increases it (look down at the ground around the hero). Hence +=.
    // History: 1224 shipped += against the broken axis mapping (the
    // engine reads CameraAngle[0] from the vertical, so the delivered
    // depression was 90 - pitch and mouse-up plunged the view at the
    // hero's feet — the inversion G reported). Train 16 flipped the sign
    // (-=) but kept the broken mapping, which made mouse-up flatten the
    // axis only as far as 52 degrees. With the mapping fixed in
    // ApplyCamera, += is the standard non-inverted response again.
    // Yaw is unchanged on purpose: forward = (sin yaw, cos yaw),
    // right = (cos yaw, -sin yaw), so dx > 0 (mouse right) grows the
    // yaw and rotates the view toward the hero's right — already the
    // standard direction (audit in CAMFIX-REPORT).
    m_Pitch += (float)dy * kPitchSens;
    if (m_Pitch < kPitchMin) m_Pitch = kPitchMin;
    if (m_Pitch > kPitchMax) m_Pitch = kPitchMax;
    m_LastLookStep = m_Step;
}

// ── Per-step update ─────────────────────────────────────────────────────

void CActionCamera::Update()
{
    // Train 15 integration: mirror Train 13's scheme state into the seam
    // flag (state compare every sim step — covers the backtick toggle AND
    // the Options-menu selector; SetSchemeAction(false) releases the lock
    // synchronously, so a flip to Classic frees the cursor this step).
    SetSchemeAction(ActionMove_IsAction());
    ++m_Step;
    if (!Engaged()) {
        ReleaseLock();
        return;
    }

    // Self-heal: if the OS/platform dropped relative mode under us
    // (focus loss), forget the lock; the state machine below re-locks
    // when conditions hold again.
    if (m_Locked && !Platform::GetInput().RelativeMouseMode())
        m_Locked = false;

    const bool wantLock = !CursorUiOpen() && !CameraTopViewEnabled;
    if (wantLock && !m_Locked) EngageLock();
    if (!wantLock && m_Locked) ReleaseLock();
    if (!m_Locked) return;

    CHARACTER* hero = HeroC();
    const float hx = hero->Object.Position[0];
    const float hy = hero->Object.Position[1];
    m_LastHeroX = hx;
    m_LastHeroY = hy;

    // MEAMU Train 22: NO camera follow. The yaw is the player's alone
    // (mouse only) — the old follow branch dragged the yaw toward the
    // hero's back while he moved, and the facing writer below then
    // chased the dragged yaw: a feedback loop that twitched the hero's
    // facing while moving/strafing (G's bug report, 2026-11-19).

    // MEAMU Train 74 (G's generalised lock law, 2026-11-20):
    // ANY target lock camera-follows -- follow is a property of
    // the LOCK itself, not of any button. Q, LB, pad auto-lock,
    // LT: whenever a verb lock stands in the Action scheme, the
    // yaw eases toward the lock's bearing (the same CreateAngle
    // the facing writer below uses, through the line-141
    // conversion); the lock breaks, the follow ends, and the yaw
    // stays where the follow left it (mouse resumes, no snap).
    // This supersedes Train 68's LT-hold-only scoped exception.
    // Train 22 stays safe: the follow target is the LOCK -- an
    // external point, not the hero's own back -- so the feedback
    // loop that law guards against cannot form. (This camera
    // exists only in the Action scheme; Classic has no action
    // camera, so a Classic lock steers facing + casts only.)
    if (TargetLock_Active()) {
        CHARACTER* tgt = EntryAt(TargetLock_Index());
        if (tgt && tgt->Object.Live != 0) {
            typedef float (__cdecl *CreateAngleFn)(float, float, float, float);
            const float ang = ((CreateAngleFn)&CreateAngle)(
                hero->Object.Position[0], hero->Object.Position[1],
                tgt->Object.Position[0],  tgt->Object.Position[1]);
            const float wantYaw = NormalizeDeg(180.0f - ang);
            float dy = NormalizeDeg(wantYaw - m_CamYaw);
            const float kFollowStep = 10.0f;   // deg per sim step (~250 deg/s)
            if (dy > kFollowStep) dy = kFollowStep;
            else if (dy < -kFollowStep) dy = -kFollowStep;
            m_CamYaw = NormalizeDeg(m_CamYaw + dy);
        }
    }

    // MEAMU Train 22: the hero faces the camera yaw whenever the camera
    // is locked and no cast animation owns the facing — idle AND moving
    // (GunZ law: hero faces the reticle, steady). This is the ONLY
    // per-frame facing writer while locked: the path-follower's turn is
    // gated off for the hero (Path_Legacy) and the movement input no
    // longer pins facing itself (ActionMove).
    if (hero->SkillStage == 0) {
        float want = NormalizeDeg(180.0f - m_CamYaw);
        // MEAMU Train 48: a target lock drives the hero's FACING (the
        // camera yaw stays the player's alone — the lock never
        // rotates the camera, per the lock sheet).
        if (TargetLock_Active()) {
            CHARACTER* tgt = EntryAt(TargetLock_Index());
            if (tgt && tgt->Object.Live != 0) {
                typedef float (__cdecl *CreateAngleFn)(float, float, float, float);
                want = ((CreateAngleFn)&CreateAngle)(
                    hero->Object.Position[0], hero->Object.Position[1],
                    tgt->Object.Position[0],  tgt->Object.Position[1]);
            }
        }
        const float cur = hero->Object.Angle[2];
        hero->Object.Angle[2] = cur + ShortestArc(cur, want) * kFacingEase;
    }

    // Keep the logical cursor at screen centre: the stock hover picker
    // raycasts through it, which is what makes aim = reticle for every
    // existing attack path (click, RMB cast, KbPlay digits).
    DAT_083a427c = (DWORD)ScreenCentreX();
    DAT_083a4278 = 240;
}

// ── Camera placement ────────────────────────────────────────────────────

bool CActionCamera::ApplyCamera(float distanceZoom)
{
    if (!Engaged() || CameraTopViewEnabled) return false;

    CHARACTER* hero = HeroC();
    const float hx = hero->Object.Position[0];
    const float hy = hero->Object.Position[1];
    const float hz = hero->Object.Position[2];

    const float rad = 0.017453292f;
    const float fx = sinf(m_CamYaw * rad);   // view forward, XY
    const float fy = cosf(m_CamYaw * rad);
    const float rx = fy, ry = -fx;           // camera right, XY
    const float cp = cosf(m_Pitch * rad);
    const float sp = sinf(m_Pitch * rad);
    const float dist = kFollowDistance * distanceZoom;

    // Aim point: hero chest, shifted to the right shoulder.
    const float tx = hx + rx * kShoulderRight;
    const float ty = hy + ry * kShoulderRight;
    const float tz = hz + kAimHeight;

    float cx = tx - fx * cp * dist;
    float cy = ty - fy * cp * dist;
    float cz = tz + sp * dist;

    // Terrain clamp: never sink below the heightmap (+ margin). v1 has
    // no building-interior handling — a roof overhead does not lift the
    // camera (documented limitation).
    const float ground = RequestTerrainHeight(cx, cy);
    if (cz < ground + kTerrainMargin) cz = ground + kTerrainMargin;

    CameraPosition[0] = cx;
    CameraPosition[1] = cy;
    CameraPosition[2] = cz;
    // MEAMU Train 18 — THE ANGLE BUG. This engine measures CameraAngle[0]
    // from the VERTICAL: GL_BeginViewport (Render/Camera.cpp) builds the
    // view as Ry(a1)·Rx(a0)·Rz(a2), whose forward is
    // (-sin a0·sin a2, -sin a0·cos a2, -cos a0), so the view-axis
    // depression is 90 - |a0|. Proof by the stock camera: a0 = -48.5
    // (gCamera3D pitch default) delivers 41.5 degrees of depression,
    // matching its position geometry (850 up / 1000 back = 40.4 deg).
    // Trains 14/16 wrote EarthQuake - m_Pitch here, i.e. they delivered
    // depression 90 - m_Pitch (band 4..38 nominal = 86..52 real: always
    // top-down, hero off-frame except at the steep end). Writing
    // EarthQuake - (90 - m_Pitch) makes the delivered depression exactly
    // m_Pitch, and the axis passes through the aim point again (the
    // position above already sits at elevation m_Pitch over the aim).
    CameraAngle[0] = EarthQuake - (90.0f - m_Pitch);
    CameraAngle[1] = 0.0f;
    CameraAngle[2] = m_CamYaw;
    CameraDistance = dist;
    return true;
}

// ── Aim ─────────────────────────────────────────────────────────────────

int CActionCamera::ReticlePick() const
{
    if (!Hero || !CharactersClient) return -1;
    CHARACTER* hero = HeroC();
    const int cx = ScreenCentreX();
    const int cy = 240;
    const int r2 = kReticleRadiusPx * kReticleRadiusPx;

    int best = -1, bestD2 = r2;
    for (int i = 0; i < 400; ++i) {
        if (!ValidReticleTarget(i)) continue;
        CHARACTER* c = EntryAt(i);
        if (Cheb(hero, c) > kReticleRangeTiles) continue;
        float p[3] = { c->Object.Position[0], c->Object.Position[1],
                       c->Object.Position[2] + 100.0f };
        int sx = -1000, sy = -1000;
        Camera_ProjectWorldToScreen(p, &sx, &sy);
        if (sx <= -1000 || sx < -100 || sx > 740 || sy < -100 || sy > 580)
            continue;   // unprojectable or far off-screen (incl. behind)
        const int dx = sx - cx, dy = sy - cy;
        const int d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) { best = i; bestD2 = d2; }
    }
    return best;
}

// MEAMU Train 21: RenderReticle() removed - G ordered the crosshair gone
// (2026-11-19). ReticlePick() above stays: aim targeting is unchanged.
