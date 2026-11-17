#pragma once

// ── MEAMU BIJU PET TRANSFORM (2026-11-16, owner's GO: all 10 at once) ────────
//
// When a player character has one of the 10 Biju pet items EQUIPPED (item
// section 13, subs 496-505 → item types 7152-7161), the character RENDERS AS
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

    // Beast index 0-9 (Shukaku..Ten-Tails) for the character, or -1.
    int BeastFor(const CHARACTER* character);

    // Resolved beast BMD model slot for the character, or -1 when not
    // transformed (also -1 when the catalog model is missing/unloaded, so
    // every hook falls back to the normal player render instead of an
    // invisible character).
    int ModelForCharacter(const CHARACTER* character);

    // ObjectPose_PrepareBones hook: if the entity is transformed, fills
    // `bones` with the beast pose and returns true (stock path skipped).
    bool PreparePose(OBJECT* entity, float* bones, char translate);

    // Draw_RenderObject hook: if the entity is transformed, draws the beast
    // body and returns true (stock dispatch skipped).
    bool DrawBody(void* entity, int translate);
}
