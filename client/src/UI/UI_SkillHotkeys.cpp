#include <cstdint>
// UI_SkillHotkeys.cpp

#include "stdafx.h"
#include "Render/Gfx.h"
#include "globals.h"
#include "functions.h"

// FindHotKey @ 0x004B1170 (~202 lines)
// Looks up a skill ID in the CharacterMachine hotkey table via MAIN_HASH_CLASS.
// Returns hotkey slot index (0..19), or 0 if not found (igual que IDA).
// Original wraps access in anti-tamper encrypt/decrypt; we skip that.
// Ghidra: CharacterAttribute->Skill[iVar5+4] == unaff_retaddr (phantom param = Skill)
// Real access: *(BYTE*)(DAT_07cf1ff4 + 0x57 + iVar5) == Skill
// IDA: FindHotKey @ 0x004B1170 — int __cdecl FindHotKey(int Skill)
//   v17 = 0;                                        // <- valor por defecto
//   v4  = 0;
//   while ( *(std::uint8_t *)(CharacterAttribute + v4 + 87) != Skill )
//     if ( ++v4 >= 20 ) goto LABEL_22;
//   v17 = v4;
// LABEL_22:
//   return v17;
//
// OJO: no devolver -1 en el camino de no-encontrado.  El retorno termina en
// CreateEffect (MoveCharacter -> CreateArrows(c, o, 0, FindHotKey(skill), ...)
// -> CreateArrow -> `i[133] = (BYTE)SkillIndex`) y sub_466440 (MoveEffect, en
// cada tick del proyectil) lee `CharacterAttribute[ i[133] + 87 ]`: con 0xFF
// sale del array de 20 skills (87..106) y puede disparar
// `CreateJoint(1249, ...)` (la espiral de Penetration) en cada flecha.
// Ningun caller del arbol compara el retorno contra -1 ni contra < 0.
// IDA: FindHotKey (0x004B1170)
int __stdcall FindHotKey(int Skill) {
    // anti-tamper hash table — skipped (encrypt CharacterMachine before read)

    char* charAttr = (char*)DAT_07cf1ff4;  // IDA: CharacterAttribute
    if (!charAttr)
        return 0;

    int slot = 0;                          // IDA: v4
    while ((unsigned char)charAttr[0x57 + slot] != (unsigned int)Skill) {
        // DESVIACION: el array Skill mide 60 slots utiles en este build
        // (el server manda hasta MAX_SKILL_LIST=60); el original cortaba
        // en 20 y devolvia 0 (slot 0) para skills mas alla, corrompiendo
        // el SKKey de los efectos (CreateArrows lee CA[ret + 87]).
        if (++slot >= 60)
            return 0;                      // IDA: goto LABEL_22 con v17 = 0
    }

    // anti-tamper hash table — skipped (decrypt CharacterMachine after read)
    return slot;                           // IDA: v17 = v4
}

// RenderSkillIcon @ 0x004BB940 (~250 lines) — SUMMARY STUB
// Renders a skill icon bitmap at (x,y) with given dimensions.
// Reads CharacterAttribute->Skill[iIndex] via encrypted hash table access.
// If skill ID is 0, renders empty slot. Otherwise renders skill texture.
// IDA: sub_4BB940 (0x004BB940)
void __cdecl RenderSkillIcon(int iIndex, float x, float y, float width, float height) {
    // 0x004BB940 — Renders a skill icon bitmap at (x,y) with given dimensions.
    // ~80% of Ghidra output is anti-tamper hash table operations wrapping reads to
    // CharacterAttribute->Skill and ->HotKey arrays. Only real logic implemented.

    // anti-tamper hash table — skipped (encrypt CharacterMachine before read)

    // Skill ID = CharacterAttribute->Skill[iIndex] (CharacterAttribute = DAT_07cf1ff4,
    // el array Skill arranca en +0x57; Ghidra lo muestra como Skill[unaff_retaddr + 4]).
    // Se acota iIndex (puede venir de Hero[913] con basura): sin eso
    // CA[0x57+iIndex] se sale del buffer y el skillId basura indexa
    // SkillAttribute fuera de rango.
    if (iIndex < 0 || iIndex >= 60) return;
    char* charAttr = (char*)DAT_07cf1ff4;
    if (!charAttr) return;
    unsigned int skillId = (unsigned char)*(charAttr + 0x57 + iIndex);

    // Skip if skillId is invalid (0 = empty, >= 64 = OOB on SkillAttribute table).
    if (skillId == 0 || skillId >= 64) {
        return;
    }

    // Skill 0x2f (47, se usa montado): IDA sub_4BB940 L91-97 tiñe el icono
    // rojizo si el heroe no tiene Uniria (818) ni Dinorant (819) en el slot de
    // helper (Hero + 696 = c+0x2B8).
    if (skillId == 0x2f) {
        const BYTE* hero = (const BYTE*)(uintptr_t)DAT_07abf5d8;
        const short helperType = hero ? *(const short*)(hero + EntityOffset(696)) : -1;
        if (helperType != 818 && helperType != 819)
            Gfx::Color3f(1.0f, 0.5f, 0.5f);
    }

    float fWidth = (float)(int)width;
    float fHeight = (float)(int)height;
    float fX = (float)(int)x;
    float fY = (float)(int)y;

    // 004BB940: skill icons are eight columns wide.  The original advances U
    // with the icon width and V with the icon height, then trims one pixel from
    // the V extent to avoid sampling the next atlas row.
    const int atlasIndex = (int)skillId - 1;
    const float atlasU = (float)((atlasIndex % 8) * (int)width);
    const float atlasV = (float)((atlasIndex / 8) * (int)height);

    // RenderBitmap(298, x, y, width, height, u, v, uWidth, vHeight, 1, 1)
    GL_DrawTexture(0x12a, fX, fY, fWidth, fHeight,
                 atlasU * _DAT_00552b7c,
                 atlasV * _DAT_00552b7c,
                 fWidth * _DAT_00552b7c,
                 (fHeight - _DAT_0055256c) * _DAT_00552b7c,
                 1, 1);

    // anti-tamper hash table — skipped (encrypt CharacterMachine before read)

    // Read hotkey assignment from CharacterAttribute->HotKey[SelectedHero][iIndex + 4]
    // Confirmed via Ghidra CHARACTER_ATTRIBUTE layout:
    //   0x53 Skill[64]      (iIndex+4 indexed here earlier)
    //   0x93 SkillLevel[64]
    //   0xD3 HotKey[4][64]  — 4 characters × 64 slots, stride 64 bytes
    // HotKey[SelectedHero][iIndex+4] = charAttr + 0xD3 + SelectedHero*64 + (iIndex + 4)
    unsigned int selectedHero = (unsigned int)DAT_005616ac;  // SelectedHero index (0..3)
    if (selectedHero > 3) selectedHero = 0;  // safety clamp
    int hotkey = (unsigned char)*(charAttr + 0xD3 + selectedHero * 64 + iIndex + 4);
    if (hotkey == 0) hotkey = 0xFF;  // empty slot → no-hotkey sentinel

    // anti-tamper hash table — skipped (decrypt CharacterMachine after read)

    if (hotkey != 0xFF) {
        RenderNumber2D(x + _DAT_00552650, y + _DAT_00552a4c, hotkey, 9.0f, 10.0f);
    }
    DAT_00559c6c = (char)hotkey;
}
// ─────────────────────────────────────────────────────────────────────────────
// Persistencia local de las hotkeys de skill — agregado del port
// (hotfix 2026-11-12, cliente v2.2-hotfix).
//
// La tabla HotKey de CharacterAttribute (fila del personaje en
// CA + 0xD7 + SelectedHero*0x40, 64 bytes; ver RenderSkillIcon) vivía sólo
// en RAM: el bloque CharacterMachine se alloca en cero al arrancar
// (Platform/ClientEntry.cpp) y ningún path volcaba la tabla a disco, así
// que las asignaciones Ctrl+1..0 que escribe Chat_InputTick se perdían al
// cerrar el cliente (nada las persistía entre sesiones).
//
// Solución: un archivo por personaje en la carpeta "Hotkeys" junto al
// ejecutable — mismo idiom de caché local que ContentCatalog con "Cache"
// (EnsureDirectory + Open/fwrite vía Platform::GetFileSystem):
//   Hotkeys/<nombre>.dat = cabecera MUHK + la fila de 64 bytes.
// SaveSkillHotkeys lo llama Chat_InputTick tras asignar; detecta si la fila
// cambió y sólo escribe en ese caso (el caller corre por frame con Ctrl
// apretado). LoadSkillHotkeys lo llama Recv_JoinMapServer al entrar al
// mundo: restaura la fila del personaje y, si no hay archivo válido, la
// deja en 0xFF (sin asignar) para que un personaje no herede las teclas
// del que ocupó antes el mismo slot de cuenta.
// ─────────────────────────────────────────────────────────────────────────────
#include "Platform/IFileSystem.h"
#include "Entity/EntityName.h"
#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kHotkeyRowOffset = 0xD7;   // fila: CA + 0xD7 + SelectedHero*0x40
constexpr int kHotkeyRowSize   = 0x40;   // 64 ranuras de skill

struct HotkeyFileHead {
    char          Magic[4];              // "MUHK"
    std::uint16_t Version;               // 1
    std::uint16_t RowSize;               // 64
};

char* CurrentHotkeyRow()
{
    if (!DAT_07cf1ff4) return nullptr;
    int hero = (int)DAT_005616ac;        // SelectedHero
    if (hero < 0 || hero > 4) hero = 0;  // mismo clamp que el handler F3/11
    return (char*)DAT_07cf1ff4 + hero * kHotkeyRowSize + kHotkeyRowOffset;
}

// Nombre del personaje actual: nombre de la entidad héroe > selección de
// char-select > campo Name[10] de CharacterAttribute.
bool CurrentCharacterName(char* out, std::size_t cap)
{
    const char* name = nullptr;
    if (DAT_07abf5d8) name = EntityName::Get((const void*)(uintptr_t)DAT_07abf5d8);
    if (!name || !name[0]) name = EntityName::Selected();
    if (name && name[0]) {
        std::size_t n = strlen(name);
        if (n >= cap) n = cap - 1;
        memcpy(out, name, n);
        out[n] = '\0';
        return true;
    }
    if (DAT_07cf1ff4) {
        const char* ca = (const char*)DAT_07cf1ff4;   // Name[10], sin NUL garantizado
        std::size_t n = 0;
        while (n < 10 && ca[n] != '\0') ++n;
        while (n > 0 && ca[n - 1] == ' ') --n;
        if (n == 0) return false;
        if (n >= cap) n = cap - 1;
        memcpy(out, ca, n);
        out[n] = '\0';
        return true;
    }
    return false;
}

// "Hotkeys/<nombre saneado>.dat" — el nombre de MU es alfanumérico, pero se
// sanea igual (cualquier otro byte → '_') para que sea un nombre de archivo
// válido en cualquier filesystem.
bool HotkeyStorePath(const char* name, char* out, std::size_t cap)
{
    const char prefix[] = "Hotkeys/";
    std::size_t n = 0;
    for (const char* p = prefix; *p && n + 1 < cap; ++p) out[n++] = *p;
    const std::size_t nameStart = n;
    for (const char* p = name; *p && n + 5 < cap; ++p) {
        const unsigned char c = (unsigned char)*p;
        out[n++] = (isalnum(c) || c == '-' || c == '_') ? (char)c : '_';
    }
    if (n == nameStart) return false;    // nombre vacío
    memcpy(out + n, ".dat", 5);          // incluye el NUL
    return true;
}

// Última fila volcada/restaurada (detección de cambio de SaveSkillHotkeys).
char          s_LastHotkeyName[64] = {};
unsigned char s_LastHotkeyRow[kHotkeyRowSize] = {};
bool          s_HotkeyCacheValid = false;

} // namespace

void __cdecl SaveSkillHotkeys(void)
{
    char* row = CurrentHotkeyRow();
    if (!row) return;
    char name[64];
    if (!CurrentCharacterName(name, sizeof(name))) return;
    if (s_HotkeyCacheValid && strcmp(name, s_LastHotkeyName) == 0 &&
        memcmp(row, s_LastHotkeyRow, kHotkeyRowSize) == 0)
        return;                          // sin cambios: no reescribir
    char path[96];
    if (!HotkeyStorePath(name, path, sizeof(path))) return;
    Platform::GetFileSystem().EnsureDirectory("Hotkeys");
    FILE* f = Platform::GetFileSystem().Open(path, "wb");
    if (!f) return;
    HotkeyFileHead head;
    memcpy(head.Magic, "MUHK", 4);
    head.Version = 1;
    head.RowSize = (std::uint16_t)kHotkeyRowSize;
    const bool ok = fwrite(&head, sizeof(head), 1, f) == 1 &&
                    fwrite(row, 1, kHotkeyRowSize, f) == (std::size_t)kHotkeyRowSize;
    fclose(f);
    if (!ok) { remove(path); return; }
    strcpy(s_LastHotkeyName, name);      // name viene acotado a 63 + NUL
    memcpy(s_LastHotkeyRow, row, kHotkeyRowSize);
    s_HotkeyCacheValid = true;
}

void __cdecl LoadSkillHotkeys(void)
{
    char* row = CurrentHotkeyRow();
    if (!row) return;
    char name[64];
    if (!CurrentCharacterName(name, sizeof(name))) return;
    // Punto de partida determinista: sin asignaciones. Si hay un archivo
    // válido del personaje, pisa la fila; si no, no hereda nada de nadie.
    memset(row, 0xFF, kHotkeyRowSize);
    char path[96];
    if (HotkeyStorePath(name, path, sizeof(path))) {
        FILE* f = Platform::GetFileSystem().Open(path, "rb");
        if (f) {
            HotkeyFileHead head;
            unsigned char buf[kHotkeyRowSize];
            if (fread(&head, sizeof(head), 1, f) == 1 &&
                memcmp(head.Magic, "MUHK", 4) == 0 && head.Version == 1 &&
                head.RowSize == kHotkeyRowSize &&
                fread(buf, 1, kHotkeyRowSize, f) == (std::size_t)kHotkeyRowSize) {
                // Saneado: sólo dígitos 0..9 o 0xFF (sin asignar).
                for (int i = 0; i < kHotkeyRowSize; ++i)
                    row[i] = (char)((buf[i] <= 9 || buf[i] == 0xFF) ? buf[i] : 0xFF);
            }
            fclose(f);
        }
    }
    // La fila restaurada ES el estado guardado: un Save inmediato no escribe.
    strcpy(s_LastHotkeyName, name);
    memcpy(s_LastHotkeyRow, row, kHotkeyRowSize);
    s_HotkeyCacheValid = true;
}
