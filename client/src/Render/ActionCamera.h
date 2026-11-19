#pragma once
// ActionCamera.h — MEAMU Train 14 (2026-11-19, G's vision): GTA/FPS-style
// action camera for the ACTION control scheme.
//
// When the ACTION scheme is on and no cursor UI owns the screen, the mouse
// is CAPTURED (pointer lock / SDL relative mouse mode): mouse movement
// rotates camera yaw + pitch freely and continuously — no button hold.
// The camera sits behind the hero at a follow distance with a slight
// right-shoulder offset, terrain-clamped; the hero faces camera yaw when
// idle; aim is the screen centre (a small reticle is drawn and the stock
// hover picker, fed a centred cursor, resolves targets through it).
//
// SEAM (Train 15 integration, WIRED): Train 13's ActionMove owns the
// control-scheme state (backtick toggle + Options selector). This module
// keeps its seam flag — SchemeIsAction() / SetSchemeAction(bool) — and
// Update() mirrors it from ActionMove_IsAction() EVERY sim step (a state
// compare, so every flip path — backtick, Options menu — is picked up
// with no extra call sites). SetSchemeAction(false) releases the pointer
// lock SYNCHRONOUSLY in that call, so flipping to Classic frees the
// cursor within the same sim step the camera sees the flip (<=40 ms).
//
// Master availability gate: Config.ini [Game] ActionCamera (UserSettings,
// default 1). The camera itself is OFF until the scheme engages it.
//
// Classic behaviour is untouched when the scheme is Classic or the feature
// is disabled: MoveMainCamera runs its stock path byte-for-byte.

class CActionCamera {
public:
    // ── Scheme seam (Train 13 at integration) ──────────────────────────
    bool SchemeIsAction() const { return m_SchemeAction; }
    void SetSchemeAction(bool on);

    bool FeatureEnabled() const;                 // [Game] ActionCamera
    bool Engaged() const;                        // feature + scheme + in world
    bool PointerLocked() const { return m_Locked; }

    // ── Input ──────────────────────────────────────────────────────────
    // Relative-mouse deltas (pixels) from Input_OnEvent while locked.
    void OnMouseDelta(int dx, int dy);

    // ── Per-sim-step (25 Hz, from MoveMainCamera) ──────────────────────
    // Lock state machine (engage / UI auto-release / re-lock), follow-yaw
    // easing, idle hero-facing easing, logical-cursor centring.
    void Update();
    // Writes CameraPosition/CameraAngle for this step and returns true
    // when the action camera owns the camera; false = stock path runs.
    // `distanceZoom` is gCamera3D.UpdateDistanceZoom() for this step
    // (passed in so the interpolation advances exactly once per step).
    bool ApplyCamera(float distanceZoom);

    // ── Aim ────────────────────────────────────────────────────────────
    // Enemy nearest the screen-centre reticle: character-table index or
    // -1. Latch-legal targets only (same predicate as KbPlay's
    // ValidLatchTarget, mirrored locally), within 14 tiles (Chebyshev)
    // of the hero and 110 logical px of screen centre. This is the
    // resolution rule for untargeted casts in Action mode; the latch /
    // AUTO acquisition stays the fallback when it returns -1.
    int ReticlePick() const;

    // Centre reticle, drawn in the HUD pass while pointer-locked.
    void RenderReticle();

    // True while any cursor-owning UI is up (inventory, character, shop,
    // warehouse, chaos mix, trade, guild, party, event/NPC dialog,
    // text/chat input, modal gate, or the clickable C++ windows). The
    // lock releases while this holds and re-locks when it clears.
    static bool CursorUiOpen();

    float GetYaw() const { return m_CamYaw; }     // CameraAngle[2] space, deg
    float GetPitch() const { return m_Pitch; }    // camera elevation, deg

private:
    void EngageLock();
    void ReleaseLock();

    bool  m_SchemeAction = false;   // mirrored from ActionMove_IsAction() each step
    bool  m_Locked       = false;
    bool  m_HasYaw       = false;
    float m_CamYaw       = -45.0f;  // degrees, CameraAngle[2] convention
    float m_Pitch        = 15.0f;   // camera elevation above the aim point (default 15° — Vice City TPS, G 2026-11-19 12:37; band 8–60°)
    float m_LastHeroX    = 0.0f;
    float m_LastHeroY    = 0.0f;
    unsigned m_Step      = 0;
    unsigned m_LastLookStep = 0;
};

extern CActionCamera gActionCamera;
