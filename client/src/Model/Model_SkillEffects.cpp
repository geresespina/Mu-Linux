// IDA: OpenImages @ 0x0050eb80 — UI_LoadInterfaceTextures
// Loads UI interface textures (inventory slots, buttons, skill icons, money
// icons, trading UI, guild badge, etc.) into texture slots 0x22 and 0x104-0x12a.
// Despite the name, this loads inventory/UI interface textures, not skill
// particle effects (those are in Model_Misc.cpp).
#include "stdafx.h"
#include "globals.h"
#include "functions.h"

// IDA: OpenImages
void __cdecl UI_LoadInterfaceTextures(void)
{
    OpenJPG("Interface\\Skill.jpg",          0x12a, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Mangekyou.jpg",      0x12b, 0x2600, 0x2900, 0, '\x01');  // MEAMU 2026-11-15: Mangekyou Sharingan icon (skill 58) -- the 256px Skill.jpg atlas has no cell for row 7, so skill 58 draws this dedicated texture full-frame (BuffBar + HUD_Pass5)
    OpenJPG("Interface\\SageMode.jpg",        0x134, 0x2600, 0x2900, 0, '\x01');  // MEAMU 2026-11-17: Sage Mode icon (skill 59) -- leaf mark, same dedicated-texture pattern as 0x12b. MEAMU 2026-11-17 (Train 5): moved OFF bitmap 300 (0x12c) -- that slot is BITMAP_HIDE, the BMD texture system's sentinel. MEAMU 2026-11-17 (Train 6): moved OFF 0x5f1 (1521) -- that slot sits inside the ContentCatalog texture region: the catalog allocator starts at BITMAP_MAX_VANILLA (1450) and marches upward, so its 72nd unique texture overwrites 1521 in-game (the same mechanism killed WDecorOld at 0x5f0). Bitmap 0x134 (308) is a verified-free gap: above the skin range 0x12d-0x133, below the equipment texture allocator base 0x136, below the catalog region, and referenced by no other load or draw.
    OpenJPG("Interface\\JinchurikiMode.jpg",  0x135, 0x2600, 0x2900, 0, '\x01');  // MEAMU 2026-11-17 (Train 4): Jinchuriki Mode icon (skill 60) -- chakra-cloak flame mark, same dedicated-texture pattern. MEAMU 2026-11-17 (Train 6): moved OFF 0x12d (301) -- that slot is BITMAP_SKIN (player skin variant 0, loaded by Model_LoadPlayerEquipmentTextures); the icon only won by load order and player skins with variant 0/1 drew the mode icons as their skin. Bitmap 0x135 (309) is the verified-free gap beside Sage's 0x134.
    OpenJPG("Interface\\BijuMode.jpg",        0x106, 0x2600, 0x2900, 0, '\x01');  // MEAMU 2026-11-17 (Train 4): Biju Mode icon (skill 61) -- tailed-beast eye mark, same dedicated-texture pattern. MEAMU 2026-11-17 (Train 6): moved OFF 0x12e (302) -- that slot is the player skin variant 1 texture (level_man022, BITMAP_SKIN+1). Bitmap 0x106 (262) is the unused gap inside this loader's own sequence (between Item_Back02 0x105 and Item_Cap 0x107), referenced by no other load or draw.
    OpenJPG("Interface\\ChakraCloak.jpg",     0x0c8, 0x2600, 0x2900, 0, '\x01');  // MEAMU 2026-11-18 (Train 3): Chakra Cloak icon (skill 73) -- crimson chakra-cloak flame mark, same dedicated-texture pattern as the other modes. Bitmap 0x0c8 (200) passed the Train 6 audit: no OpenJPG/OpenTGA/OpenTexture load and no GL_DrawTexture draw in the texture namespace targets it (Model_Misc's 0xc8 is a MODEL id, a different table -- the BloodGore 0x103 precedent), and it sits below every allocator march (BITMAP_SKIN 301..307, equipment TextureCurrent base 310, ContentCatalog region 1450+). Final proof is in-game, as with every MEAMU slot.
    OpenJPG("Interface\\DragonSageMode.jpg",  0x0c9, 0x2600, 0x2900, 0, '\x01');  // MEAMU 2026-11-18 (Train 3): Dragon Sage Mode icon (skill 75) -- emerald dragon-sage spiral mark, same dedicated-texture pattern. Bitmap 0x0c9 (201): the verified-free slot beside Chakra Cloak's 0x0c8, same audit (Model_Misc's 0xc9 is likewise a MODEL id, not a texture).
    OpenTGA("Interface\\BijuProc.tga",        0x100, 0x2600, 0x2900, 0, '\x01');  // MEAMU 2026-11-18 (Train 2, Module B): Biju Powers proc-visual sprite (one soft disc, TGA/.ozt for real alpha; tinted per beast at spawn). Bitmap 0x100 (256) audited free in Net/BijuProcVisuals.cpp (no load/draw/particle case; below the skin march 301+, equipment base 310 and the catalog region 1450+). Particle type 256 rides this same slot.
    OpenJPG("Interface\\BijuPowers.jpg",      0x102, 0x2600, 0x2900, 0, '\x01');  // MEAMU 2026-11-17 (Biju Powers build; wired for the buff row in Train 2 Module A): the ten Biju Power glyphs (skills 62..71) as a 5x2 atlas. Bitmap 0x102 (258) is the verified-free gap in this loader's own sequence (between Item_Money 0x101-region loads and Item_Back01 0x104; 0x103 is BloodGore): no other load or draw references it (the remaining 0x102s in the tree are a sound-file index and struct offsets -- different namespaces).
    OpenTGA("Interface\\BloodGore.tga",       0x103, 0x2600, 0x2900, 0, '\x01');  // MEAMU 2026-11-17: Blood & Gore atlas (droplet + splat cells) -- TGA/.ozt because JPEG/.ozj has no alpha channel and the splat needs real alpha. Bitmap 0x103 (259) is the verified-free gap beside BijuPowers' 0x102: no OpenJPG/OpenTGA load and no draw references it (Model_Misc's 0x103 is the Protect MODEL id, a different table; effect type 259 is the effect pool, also different). Particle type 259 rides this same slot (Net/BloodGore.h).
    OpenJPG("Interface\\Item_Back01.jpg",    0x104, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_Back02.jpg",    0x105, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_Cap.jpg",       0x107, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_Upper.jpg",     0x108, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_Lower.jpg",     0x109, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_Weapon01.jpg",  0x10a, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_Gloves.jpg",    0x10b, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_Boots.jpg",     0x10c, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_NeckLace.jpg",  0x10d, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_Ring.jpg",      0x10e, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_Money.jpg",     0x10f, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Money_01.jpg",       0x110, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Money_02.jpg",       0x111, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_Wing.jpg",      0x112, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_Fairy.jpg",     0x113, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_Weapon02.jpg",  0x114, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Item_box.jpg",       0x115, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\InventoryBox2.jpg",  0x116, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\Trading_line.jpg",   0x117, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\exit_01.jpg",        0x118, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\exit_02.jpg",        0x119, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\buy_01.jpg",         0x11a, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\buy_02.jpg",         0x11b, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\sell_01.jpg",        0x11c, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\sell_02.jpg",        0x11d, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\repair_01.jpg",      0x11e, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\repair_02.jpg",      0x11f, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\level_01.jpg",       0x120, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\level_02.jpg",       0x121, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\level_02.jpg",       0x121, 0x2600, 0x2900, 0, '\x01'); // original has duplicate
    OpenJPG("Interface\\accept_box01.jpg",   0x122, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\accept_box02.jpg",   0x123, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\mix_button1.jpg",    0x124, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\mix_button2.jpg",    0x125, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\lock_01.jpg",        0x126, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\lock_02.jpg",        0x127, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\lock_03.jpg",        0x128, 0x2600, 0x2900, 0, '\x01');
    OpenJPG("Interface\\lock_04.jpg",        0x129, 0x2600, 0x2900, 0, '\x01');
    OpenTGA("Interface\\guild.tga",          0x22,  0x2600, 0x2900, 0, '\x01');
}
