#pragma once

// MEAMU BOOST STRIP -- wire spec for the server -> client F3/F8
// "BoostState" packet (GS: CustomBoostStrip.h is the sender; this
// file is the client's copy of the contract). C2, PSWMSG_HEAD:
//
//   +0  C2
//   +1  size hi, +2 size lo          (total packet size)
//   +3  0xF3
//   +4  0xF8                         (0xF7 is the catalog's map
//                                     music -- do not reuse)
//   +5  Flags (BYTE):
//         0x01 Jinchuriki Mode ON
//         0x02 Biju form active (transformed)
//         0x04 Sage Mode ON
//         0x08 Mangekyou ON
//         0x10 soul pet EQUIPPED (informational; no chip keys
//              off it since the 2026-11-17 drain ruling)
//         0x20 war bonus chip present (clan member, bonus > 0)
//         0x40 / 0x80 reserved (ignored on receive)
//   +6  WarBonusPct (BYTE): clan weekly damage bonus % (0..50);
//       0 when clan-less.
//   +7  DrainPctPerSec (BYTE): the ACTIVE beast mode's HP drain,
//       percent per second -- JinchurikiDrainPercentPerSec while
//       Jinchuriki Mode is ON, BijuDrainPercentPerSec while in
//       Biju form, 0 otherwise; read server-side from BijuHunt.txt
//       (never hardcoded client-side). (This byte carried the
//       retired per-attack blood price until the 2026-11-17
//       drain ruling; the position never moved.)
//   +8  TotalMulX1000 (WORD, little-endian): headline damage
//       multiplier x1000, computed server-side from result-stage
//       factors only (Jinchuriki x2 . Sage/Mangekyou x1.25 . war
//       bonus x(1+bonus/100)). The Biju form is EXCLUDED (a
//       band/transform, not a multiplier): while the form flag is
//       set the headline chip reads "FORM" and this field still
//       carries the normal-form product.
//   +10 PowerCount (BYTE, 0..4)
//   +11 Power entries x PowerCount, 5 bytes each:
//         +0 SkillId (BYTE, 62..71 = the Biju Powers actives)
//         +1 Counter (WORD, little-endian: stacks / shield % /
//            orbs / charges -- per-skill meaning)
//         +3 Aux     (WORD, little-endian: per-skill spare)
//   Total size = 11 + 5 * PowerCount (max 31).
//
// The form pool % is NOT on the wire: while transformed, the
// hero's Life/MaxLife (gHeroVitals) ARE the form pool, so the
// strip renders the % from local vitals at draw time and it can
// never go stale between state packets.
//
// EXTENSIBILITY: the Biju Powers build fills the power entries
// server-side WITHOUT a protocol change; this build's server
// always sends PowerCount 0, and the render path below already
// draws any entries that arrive.
