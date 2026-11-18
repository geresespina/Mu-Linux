#pragma once

// NOTA (CI round 2, 2026-11-18): este header NO incluye Item/ItemDefines.h.
// Ese include arrastraba Platform/PlatformHeaders.h (headers de red de
// Windows) a todo TU que incluye BijuTransform.h — en particular a los
// harnesses PoseBatchNative/PoseEntityPointer, que inlinean este header en
// un main.cpp generado con otro orden de includes y chocaban con la guerra
// winsock v1/v2 (C2011 sockaddr/fd_set/timeval). La decodificación
// (sección, sub) vive en la definición, en BijuTransform.cpp.

// ── MEAMU BIJU PET TRANSFORM (2026-11-16, owner's GO: all 10 at once) ────────
//
// When a player character has one of the 10 Biju pet items EQUIPPED (item
// section 13, subs 496-505 — raw types arrive in the fork's EXTENDED index
// space, wire 7216-7225; resolved via GetItemSection/GetItemSub), the
// character RENDERS AS
// THE BIJU: the body becomes the wild beast's monster BMD (monster classes
// 155-164, resolved live through the ContentCatalog, so the transform always
// matches whatever model/scale the server gives the wild beast), and all
// armour / weapons (incl. on-back) / wings / equipment renders are hidden.
// Name + guild text are drawn by other systems and stay untouched.
//
// The client never persists the equipped pet's ITEM TYPE on the CHARACTER
// (only the imitated vanilla model survives in Helper.Type / catalog parts),
// so this module keeps a side table keyed by pool slot, fed by every path
// that sees the raw pet item type:
//   hero:    SetCharacterClass + Hero_ApplyCatalogEquipment (CharacterMachine)
//   remotes: ApplyCustomEquipment (F3:EB / F3:ED) + Recv_ChangePlayer slot 8
//   cleared: ChangeCharacterExt (CharSet never carries a custom pet) and
//            CreateCharacterPointer (fresh entity in the slot)
//
// TRAIN 4 FOLLOW-UP (2026-11-17): equipping the pet alone no longer
// transforms. Resolve() additionally requires the Biju Mode state bit
// 0x2000 in the persistent-state DWORD at entity+120 (same channel as
// Jinchuriki 0x1000 / Mangekyou 0x0400 / Sage 0x0800, local + remote).
// Pet equipped with no mode active = normal human render with the pet
// visible beside them; BeastFor()/JinchurikiTint() stay pet-keyed (the
// Jinchuriki aura needs the beast identity without the form).
//
// Render hooks:
//   PreparePose  — called from ObjectPose_PrepareBones: computes the entity's
//                  bone pose from the BEAST model instead of the player
//                  skeleton, translating the player's action/frame into the
//                  beast's action space (see the mapping in the .cpp).
//   DrawBody     — called from Draw_RenderObject: skins + draws the beast
//                  model as the character's body (the same LAB_simple_render
//                  call a wild monster gets), replacing the player parts.
//   RenderCharacter gates on ModelForCharacter() to skip the human renders.
//
// Fenrir IS the pet slot, so a transformed character can never also be
// mounted: the beast item occupies the one helper slot a mount would use.

struct CHARACTER;
struct OBJECT;

namespace BijuTransform
{
    // Feed / clear the side table. itemType is the raw equipped pet item
    // type (or -1 / 0xFFFF when the slot is empty or holds something else).
    void NotePetItem(void* character, int itemType);
    void Clear(void* character);

    // MEAMU SKIN BODY FIX (2026-11-18, G's live verdict on the worn Kyubi
    // skin): a full-body SKIN worn in the armor slot (item section 8,
    // sub >= 50 — Kyubi 50, Konan 51, Killer Bee 52, Naruto Sage 53, and
    // every later skin at 54+) is a complete body: it carries its own head
    // and encloses every region the base parts draw. While one is worn,
    // RenderCharacter suppresses ALL base body parts (head/helm,
    // pants, gloves, boots) and draws only the armor part — the skin
    // itself. Held items (weapons/shield) and wings render outside the
    // body-part loop and are untouched. Stock + custom set armors all sit
    // below sub 50, so normal armor never trips the test. The decode is
    // the client's own (section, sub) mapping, same as NotePetItem.
    // Sólo la declaración: la definición vive en BijuTransform.cpp (que
    // sí incluye Item/ItemDefines.h), para que este header no arrastre la
    // decodificación — ni los headers de plataforma — a sus includers.
    bool IsFullBodySkinItem(int itemType);

    // Feed / query the armor side table (same feed points as the pet
    // table: hero SetCharacterClass + Hero_ApplyCatalogEquipment,
    // remotes ApplyCustomEquipment + Recv_ChangePlayer slot 3; cleared
    // by Clear()). itemType is the raw equipped ARMOR item type (or
    // -1 / 0xFFFF when the slot is empty or holds something else).
    void NoteArmorItem(void* character, int itemType);
    bool WearsFullBodySkin(const CHARACTER* character);

    // Beast index 0-9 (Shukaku..Ten-Tails) for the character, or -1.
    int BeastFor(const CHARACTER* character);

    // MEAMU Train 4 (2026-11-17): Jinchuriki Mode aura tint for the
    // character's equipped beast (tails 1-10 -> beast index 0-9).
    // Writes RGB floats 0..1 and returns true; false when no soul pet
    // is known for the character (caller falls back to Kurama gold).
    bool JinchurikiTint(const CHARACTER* character, float out[3]);

    // Resolved beast BMD model slot for the character, or -1 when not
    // transformed (also -1 when the catalog model is missing/unloaded, so
    // every hook falls back to the normal player render instead of an
    // invisible character).
    int ModelForCharacter(const CHARACTER* character);

    // MEAMU TRAIN 7 (2026-11-17, G's idea): Jinchuriki Mode skin override.
    // While the Jinchuriki Mode state bit (0x1000, entity+120) is set, the
    // character VISUALLY wears the Naruto Kyubi skin — item (8,50), the
    // single full-body BMD at Data/Player/Custom/50/SkinNarutoKyubi.bmd —
    // in place of the armour part, with pants/gloves/boots suppressed
    // (the skin body encloses those regions). The HELM part is never
    // touched: the player's real helm (or bare head) renders as normal.
    // Pure client visual: no server, item or stat involvement. Returns
    // the catalog model slot of the skin, or -1 when the override does
    // not apply (mode off, Biju Mode 0x2000 set — the full transform
    // wins —, non-player entity, or the skin model is not loaded, so
    // every hook falls back to the normal player render).
    int JinchurikiSkinModel(const CHARACTER* character);

    // MEAMU TRAIN 8 (2026-11-17, G's live verdict on v20261117-2023): the
    // Kyubi skin BMD carries its OWN head geometry, so with Train 7 the
    // player's real head rendered INSIDE the skin's head ("my head is in
    // its head"). The skin is a SINGLE mesh (10,160 verts / 18,688 tris),
    // so there is no head mesh to hide: SkinHeadlessBegin/End bracket the
    // skin's RenderPartObject calls in RenderCharacter and swap mesh 0's
    // triangle list for a pre-filtered copy with every triangle whose 3
    // vertices are ALL bound to the head bones removed (7,953 tris; the
    // head bones — Bip01 Head / Ponytail1 / Ponytail11 — are resolved by
    // NAME from the model's own skeleton at build time). The 41 seam
    // triangles mixing head verts with Spine1 (neck/collar transition)
    // are KEPT, so no hole opens between the player's neck and the
    // skin's torso. Fail-safe: any mismatch against the shipped skin's
    // profile (counts or bone names) leaves the draw untouched, i.e.
    // the Train 7 look, never a corrupted draw.
    void SkinHeadlessBegin(int modelIdx);
    void SkinHeadlessEnd(int modelIdx);

    // ObjectPose_PrepareBones hook: if the entity is transformed, fills
    // `bones` with the beast pose and returns true (stock path skipped).
    bool PreparePose(OBJECT* entity, float* bones, char translate);

    // Draw_RenderObject hook: if the entity is transformed, draws the beast
    // body and returns true (stock dispatch skipped).
    bool DrawBody(void* entity, int translate);
}
