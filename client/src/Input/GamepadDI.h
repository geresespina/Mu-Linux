#pragma once
// GamepadDI.h — DirectInput -> unified pad state translation (padfix, 2026-11-13).
//
// Why this exists: rev.5's Gamepad.cpp read pads ONLY through SDL3's gamepad
// API (SDL_GetGamepads / SDL_OpenGamepad). On Windows that API surfaces a
// device only when SDL has a gamepad mapping for it and an enabled SDL
// joystick driver claims it — a plain DirectInput pad such as the Logitech
// Dual Action never appears, so the module saw "no pad" while Windows
// (joy.cpl) showed the device perfectly healthy. The DirectInput backend in
// Gamepad.cpp (Windows only) enumerates DI8 game controllers natively, reads
// DIJOYSTATE2 every frame, and translates it HERE into the exact internal
// pad state the SDL path produces, so the mapping layer (MAPPING.md
// semantics) is shared and unchanged.
//
// This header is deliberately platform-neutral (no Windows/SDL headers): the
// translation is pure logic over plain arrays, unit-tested with synthetic
// DIJOYSTATE2-shaped inputs. The Windows code in Gamepad.cpp copies the real
// DIJOYSTATE2 fields into DIPadRawState (plus per-axis presence + the axis
// ranges read back from the device) and calls DIPadTranslate().
//
// Semantic slots mirror SDL_GamepadAxis / SDL_GamepadButton values so the
// mapping layer can index state with its existing SDL constants (Gamepad.cpp
// static_asserts the equality of every constant used).

// ── Semantic axes (values == SDL_GamepadAxis) ───────────────────────────────
enum DIPadAxisSlot {
    DIPAD_AXIS_LEFTX = 0,
    DIPAD_AXIS_LEFTY = 1,
    DIPAD_AXIS_RIGHTX = 2,
    DIPAD_AXIS_RIGHTY = 3,
    DIPAD_AXIS_LEFT_TRIGGER = 4,   // 0 (rest) .. 32767 (full) — SDL trigger convention
    DIPAD_AXIS_RIGHT_TRIGGER = 5,
    DIPAD_AXIS_SLOTS = 6
};

// ── Semantic buttons (values == SDL_GamepadButton) ──────────────────────────
enum DIPadButtonSlot {
    DIPAD_BTN_SOUTH = 0,        // bottom face button (Cross)  -> "A"
    DIPAD_BTN_EAST = 1,         // right face button (Circle)  -> "B"
    DIPAD_BTN_WEST = 2,         // left face button (Square)   -> "X"
    DIPAD_BTN_NORTH = 3,        // top face button (Triangle)  -> "Y"
    DIPAD_BTN_BACK = 4,
    DIPAD_BTN_GUIDE = 5,
    DIPAD_BTN_START = 6,
    DIPAD_BTN_LEFT_STICK = 7,   // L3
    DIPAD_BTN_RIGHT_STICK = 8,  // R3
    DIPAD_BTN_LEFT_SHOULDER = 9,   // LB
    DIPAD_BTN_RIGHT_SHOULDER = 10, // RB
    DIPAD_BTN_DPAD_UP = 11,
    DIPAD_BTN_DPAD_DOWN = 12,
    DIPAD_BTN_DPAD_LEFT = 13,
    DIPAD_BTN_DPAD_RIGHT = 14,
    DIPAD_BUTTON_SLOTS = 32     // >= any SDL_GAMEPAD_BUTTON_COUNT (21..26)
};

// ── Raw device axes (order == DIJOYSTATE2 axis fields lX..lRz) ──────────────
enum DIRawAxis {
    DIRA_X = 0,
    DIRA_Y = 1,
    DIRA_Z = 2,
    DIRA_RX = 3,
    DIRA_RY = 4,
    DIRA_RZ = 5,
    DIRA_SLOTS = 6
};

// Raw snapshot of one DirectInput poll, in DIJOYSTATE2 shape, plus the
// per-axis metadata the backend read from the device at adopt time:
//   axisPresent[i] — the device actually exposes that axis object
//   axisMin/Max[i] — the DIPROP_RANGE read back for that axis (values in the
//                    snapshot are expressed in that range; default 0..65535)
struct DIPadRawState {
    long axis[DIRA_SLOTS] = {};
    long axisMin[DIRA_SLOTS] = { 0, 0, 0, 0, 0, 0 };
    long axisMax[DIRA_SLOTS] = { 65535, 65535, 65535, 65535, 65535, 65535 };
    bool axisPresent[DIRA_SLOTS] = { false, false, false, false, false, false };
    unsigned long pov[4] = { 0xFFFFFFFFul, 0xFFFFFFFFul, 0xFFFFFFFFul, 0xFFFFFFFFul };
    unsigned char buttons[128] = {};   // pressed when (byte & 0x80) != 0
};

// Unified pad state in SDL gamepad semantics — what the mapping layer reads
// no matter which backend (SDL gamepad / DirectInput) produced it.
struct DIPadState {
    short axes[DIPAD_AXIS_SLOTS] = {};       // sticks -32768..32767, triggers 0..32767
    bool buttons[DIPAD_BUTTON_SLOTS] = {};
};

namespace DIPadDetail {

// Normalize one raw axis value from its device range into SDL stick
// convention (-32768..32767, center ~0). Degenerate ranges read as centered.
inline short NormalizeAxis(long v, long mn, long mx) {
    if (mx <= mn) return 0;
    if (v < mn) v = mn;
    if (v > mx) v = mx;
    const double t = (double)(v - mn) / (double)(mx - mn);   // 0..1
    long out = (long)(t * 65535.0 + 0.5) - 32768;
    if (out < -32768) out = -32768;
    if (out > 32767) out = 32767;
    return (short)out;
}

inline bool RawButton(const DIPadRawState& raw, int idx) {
    return idx >= 0 && idx < 128 && (raw.buttons[idx] & 0x80) != 0;
}

} // namespace DIPadDetail

// Translate one raw DirectInput snapshot into unified pad state.
//
// Button map — POSITION-based, DualShock geometry (the Logitech Dual Action
// follows it; positions, not labels, so it holds across DualShock-layout
// pads). Indices are DirectInput's 0-based button numbers; the Dual Action's
// printed 1-based numbers are shown in parentheses:
//
//   DI 0 (btn 1)  left face  (Square)   -> WEST  ("X")
//   DI 1 (btn 2)  bottom face (Cross)  -> SOUTH ("A")
//   DI 2 (btn 3)  right face (Circle)  -> EAST  ("B")
//   DI 3 (btn 4)  top face (Triangle)  -> NORTH ("Y")
//   DI 4 (btn 5)  LB                    -> LEFT_SHOULDER
//   DI 5 (btn 6)  RB                    -> RIGHT_SHOULDER
//   DI 6 (btn 7)  LT (digital)          -> LEFT_TRIGGER axis (0 / 32767)
//   DI 7 (btn 8)  RT (digital)          -> RIGHT_TRIGGER axis (0 / 32767)
//   DI 8 (btn 9)  Back                  -> BACK
//   DI 9 (btn 10) Start                 -> START
//   DI 10 (btn 11) L3                   -> LEFT_STICK
//   DI 11 (btn 12) R3                   -> RIGHT_STICK
//
// Sticks: left = X/Y. Right: primary pair Z (X) + Rz (Y) — the Dual Action's
// right stick, as Windows' own joy.cpl shows ("Z Axis" / "Z Rotation").
// Devices that expose Rx/Ry instead of (or besides) Z/Rz fall back to the
// Rx/Ry pair per-axis when the Z/Rz axis object is absent.
//
// D-pad: POV hat 0, angle in hundredths of a degree (0 = up, clockwise),
// 0xFFFF loword = centered. Diagonals set two directions.
inline void DIPadTranslate(const DIPadRawState& raw, DIPadState& out) {
    out = DIPadState{};
    using namespace DIPadDetail;

    const auto norm = [&raw](int a) -> short {
        return NormalizeAxis(raw.axis[a], raw.axisMin[a], raw.axisMax[a]);
    };

    out.axes[DIPAD_AXIS_LEFTX] = norm(DIRA_X);
    out.axes[DIPAD_AXIS_LEFTY] = norm(DIRA_Y);
    out.axes[DIPAD_AXIS_RIGHTX] = norm(raw.axisPresent[DIRA_Z] || !raw.axisPresent[DIRA_RX] ? DIRA_Z : DIRA_RX);
    out.axes[DIPAD_AXIS_RIGHTY] = norm(raw.axisPresent[DIRA_RZ] || !raw.axisPresent[DIRA_RY] ? DIRA_RZ : DIRA_RY);

    out.axes[DIPAD_AXIS_LEFT_TRIGGER] = RawButton(raw, 6) ? (short)32767 : (short)0;
    out.axes[DIPAD_AXIS_RIGHT_TRIGGER] = RawButton(raw, 7) ? (short)32767 : (short)0;

    out.buttons[DIPAD_BTN_WEST] = RawButton(raw, 0);
    out.buttons[DIPAD_BTN_SOUTH] = RawButton(raw, 1);
    out.buttons[DIPAD_BTN_EAST] = RawButton(raw, 2);
    out.buttons[DIPAD_BTN_NORTH] = RawButton(raw, 3);
    out.buttons[DIPAD_BTN_LEFT_SHOULDER] = RawButton(raw, 4);
    out.buttons[DIPAD_BTN_RIGHT_SHOULDER] = RawButton(raw, 5);
    out.buttons[DIPAD_BTN_BACK] = RawButton(raw, 8);
    out.buttons[DIPAD_BTN_START] = RawButton(raw, 9);
    out.buttons[DIPAD_BTN_LEFT_STICK] = RawButton(raw, 10);
    out.buttons[DIPAD_BTN_RIGHT_STICK] = RawButton(raw, 11);

    const unsigned long pov = raw.pov[0];
    if ((pov & 0xFFFFul) != 0xFFFFul) {
        const long angle = (long)(pov % 36000ul);                    // 0..35999
        const int sector = (int)(((angle + 2250) % 36000) / 4500);   // 0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW
        out.buttons[DIPAD_BTN_DPAD_UP] = (sector == 0 || sector == 1 || sector == 7);
        out.buttons[DIPAD_BTN_DPAD_RIGHT] = (sector == 1 || sector == 2 || sector == 3);
        out.buttons[DIPAD_BTN_DPAD_DOWN] = (sector == 3 || sector == 4 || sector == 5);
        out.buttons[DIPAD_BTN_DPAD_LEFT] = (sector == 5 || sector == 6 || sector == 7);
    }
}

// ── XInput translation (padfix base-path fix, 2026-11-13) ───────────────────
// The native Windows XInput backend in Gamepad.cpp loads XInput itself
// (xinput1_4.dll -> xinput9_1_0.dll -> xinput1_3.dll) and polls all four
// slots every frame, because SDL's Windows joystick stack never surfaced
// ANY pad on the target machine (neither an Xbox 360 pad nor a DirectInput
// pad) even though the identical module code worked through SDL on Linux.
// Raw values below mirror XINPUT_GAMEPAD's fields; translation targets the
// same unified DIPadState the SDL and DirectInput paths produce.

// XINPUT_GAMEPAD button bits (stable ABI since XInput 1.0).
enum XIPadButtonBit : unsigned short {
    XI_BIT_DPAD_UP = 0x0001,
    XI_BIT_DPAD_DOWN = 0x0002,
    XI_BIT_DPAD_LEFT = 0x0004,
    XI_BIT_DPAD_RIGHT = 0x0008,
    XI_BIT_START = 0x0010,
    XI_BIT_BACK = 0x0020,
    XI_BIT_LEFT_THUMB = 0x0040,   // L3
    XI_BIT_RIGHT_THUMB = 0x0080,  // R3
    XI_BIT_LEFT_SHOULDER = 0x0100,
    XI_BIT_RIGHT_SHOULDER = 0x0200,
    XI_BIT_A = 0x1000,
    XI_BIT_B = 0x2000,
    XI_BIT_X = 0x4000,
    XI_BIT_Y = 0x8000
};

struct XIPadRawState {
    short thumbLX = 0, thumbLY = 0;   // XInput convention: Y up-positive
    short thumbRX = 0, thumbRY = 0;
    unsigned char leftTrigger = 0;    // 0..255
    unsigned char rightTrigger = 0;   // 0..255
    unsigned short buttons = 0;       // XIPadButtonBit mask
};

namespace XIPadDetail {
inline short NegateStick(short v) { return v == (short)-32768 ? (short)32767 : (short)-v; }
inline short ScaleTrigger(unsigned char v) { return (short)(((unsigned)v * 32767u + 127u) / 255u); }
} // namespace XIPadDetail

// Standard Xbox layout — the same mapping SDL applies to XInput devices:
// face buttons by position (A bottom, B right, X left, Y top), analog
// triggers scaled to SDL's 0..32767 trigger convention, stick Y negated
// (XInput is up-positive; the unified state/SDL convention is down-positive).
inline void XIPadTranslate(const XIPadRawState& raw, DIPadState& out) {
    out = DIPadState{};
    using namespace XIPadDetail;

    out.axes[DIPAD_AXIS_LEFTX] = raw.thumbLX;
    out.axes[DIPAD_AXIS_LEFTY] = NegateStick(raw.thumbLY);
    out.axes[DIPAD_AXIS_RIGHTX] = raw.thumbRX;
    out.axes[DIPAD_AXIS_RIGHTY] = NegateStick(raw.thumbRY);
    out.axes[DIPAD_AXIS_LEFT_TRIGGER] = ScaleTrigger(raw.leftTrigger);
    out.axes[DIPAD_AXIS_RIGHT_TRIGGER] = ScaleTrigger(raw.rightTrigger);

    const unsigned short b = raw.buttons;
    out.buttons[DIPAD_BTN_SOUTH] = (b & XI_BIT_A) != 0;
    out.buttons[DIPAD_BTN_EAST] = (b & XI_BIT_B) != 0;
    out.buttons[DIPAD_BTN_WEST] = (b & XI_BIT_X) != 0;
    out.buttons[DIPAD_BTN_NORTH] = (b & XI_BIT_Y) != 0;
    out.buttons[DIPAD_BTN_LEFT_SHOULDER] = (b & XI_BIT_LEFT_SHOULDER) != 0;
    out.buttons[DIPAD_BTN_RIGHT_SHOULDER] = (b & XI_BIT_RIGHT_SHOULDER) != 0;
    out.buttons[DIPAD_BTN_BACK] = (b & XI_BIT_BACK) != 0;
    out.buttons[DIPAD_BTN_START] = (b & XI_BIT_START) != 0;
    out.buttons[DIPAD_BTN_LEFT_STICK] = (b & XI_BIT_LEFT_THUMB) != 0;
    out.buttons[DIPAD_BTN_RIGHT_STICK] = (b & XI_BIT_RIGHT_THUMB) != 0;
    out.buttons[DIPAD_BTN_DPAD_UP] = (b & XI_BIT_DPAD_UP) != 0;
    out.buttons[DIPAD_BTN_DPAD_DOWN] = (b & XI_BIT_DPAD_DOWN) != 0;
    out.buttons[DIPAD_BTN_DPAD_LEFT] = (b & XI_BIT_DPAD_LEFT) != 0;
    out.buttons[DIPAD_BTN_DPAD_RIGHT] = (b & XI_BIT_DPAD_RIGHT) != 0;
}
