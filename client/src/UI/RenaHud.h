#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>

// RenaHud — MEAMU Train 8 (2026-11-19, G: "can you display it some
// where on our UI bar? with the Rena icon? that would look cool").
//
// Shows the hero's Rena balance on the main bottom UI bar: the Rena
// item's own 3D model (item 14,21 — rendered by RenderItem3D in the
// hotbar 3D pass, the same path the inventory grid uses) at the
// bar's bottom-left corner, with the balance as a right-aligned
// number beside it, drawn in the sub_4BD650 pass next to the HP/MP
// numbers.
//
// The balance is a server-side COUNTER (GS rena-counter build), not
// bag items: the GS pushes it at login and after every change. The
// HUD stays hidden until the first push arrives, so an older GS
// (which never sends it) simply shows nothing — no zero flashing.
//
// ════════════════════════════════════════════════════════════════
// RENA BALANCE PACKET — RECONCILED (2026-11-19)
// Designed as a placeholder before the GS build's RENA-REPORT.md
// (~/workspace/mu-97k/builds/rena-counter-2026-11-19/, section 6)
// landed; when it landed, its spec matched this block EXACTLY
// (same opcode, subcode, offset and size — the GS side picked
// 0xFA independently and proved it free on both sides). No
// reconciliation change was needed. If the wire ever changes,
// every parse decision in the client flows through the three
// constants below and the single Receive() in RenaHud.cpp;
// nothing else needs to change.
//
//   Frame: C1 plain (header.set, no encryption — the F3/06 pattern)
//     [0] 0xC1
//     [1] size = 8
//     [2] 0xF3  (this fork's custom family: F5 PetSquad, F6
//                MapTrack, F7 HpBars, F8 Boost, F9 BijuProc)
//     [3] 0xFA  (next free F3 subcode in NetRecv_F3)
//     [4..7] balance, DWORD little-endian (server cap 999,999,999)
// ════════════════════════════════════════════════════════════════
namespace RenaHud {

constexpr uint8_t kRenaPacketHead    = 0xF3;
constexpr uint8_t kRenaPacketSub     = 0xFA;  // RENA BALANCE (RENA-REPORT.md S6)
constexpr int     kRenaBalanceOffset = 4;     // uint32 LE balance
constexpr int     kRenaPacketMinSize = 8;     // full frame size

// Rena item identity (GET_ITEM(14,21) = 14*512 + 21).
constexpr int kRenaItemType = 7189;

// Bar anchor (640x480 UI space, bottom-left corner of the bar):
// the icon occupies (8,437) 22x22; the number's right edge is fixed
// at x=95 with its top at y=443. Free space by construction: the HP
// orb starts at x=97, the hero coordinate readouts sit at y>=462,
// the hotbar items start at x=208, the chat box at x>=186/y<=444.
constexpr float kIconX = 8.0f;
constexpr float kIconY = 437.0f;
constexpr float kIconSize = 22.0f;
constexpr float kNumberRightEdge = 95.0f;
constexpr float kNumberY = 443.0f;

// The one packet parser: validates a frame against the constants
// above and extracts the balance. Pure + header-inline so the
// Train 8 proof harness exercises this exact code end-to-end;
// Receive() (RenaHud.cpp) is its only caller.
inline bool ParseBalancePacket(const uint8_t* msg, int size, uint32_t& out)
{
    if (!msg || size < kRenaPacketMinSize) return false;
    if (msg[2] != kRenaPacketHead || msg[3] != kRenaPacketSub) return false;
    out = (uint32_t)msg[kRenaBalanceOffset]
        | ((uint32_t)msg[kRenaBalanceOffset + 1] << 8)
        | ((uint32_t)msg[kRenaBalanceOffset + 2] << 16)
        | ((uint32_t)msg[kRenaBalanceOffset + 3] << 24);
    return true;
}

// F3/FA receive handler (wired in NetRecv_F3). Runs the packet
// through ParseBalancePacket and stores the balance; malformed
// packets are dropped and the previous balance (if any) is kept.
void Receive(const uint8_t* msg, int size);

// True once at least one valid balance packet has arrived.
bool HasBalance();

// Last pushed balance (valid only when HasBalance()).
uint32_t Balance();

// Thousands-separated formatting ("12345" -> "12,345"). Pure and
// header-inline so the Train 8 proof harness exercises this exact
// function, not a copy. Returns buf.
inline char* FormatBalance(uint32_t value, char* buf, size_t cap)
{
    if (!buf || cap == 0) return buf;
    char digits[16];
    int n = std::snprintf(digits, sizeof(digits), "%u", (unsigned)value);
    if (n < 0) { buf[0] = '\0'; return buf; }
    size_t out = 0;
    for (int i = 0; i < n; ++i) {
        if (i > 0 && ((n - i) % 3) == 0) {
            if (out + 1 >= cap) break;
            buf[out++] = ',';
        }
        if (out + 1 >= cap) break;
        buf[out++] = digits[i];
    }
    buf[out] = '\0';
    return buf;
}

// 2D half: the balance number. Called at the end of
// Render_HudPass_4BD650_ (HUD_Pass5.cpp), after the item-count
// badges, before GL_End2D — the same pass and neighbourhood as the
// HP/MP numbers. No-op until the first balance packet arrives.
void RenderNumber2D();

// 3D half: the Rena item model. Called inside
// Render_HotbarItems3D_ (HUD_Pass1.cpp) after the hotbar slot loop,
// riding that pass's item projection exactly like the hotbar items
// themselves. No-op until the first balance packet arrives.
void RenderIcon3D();

} // namespace RenaHud
