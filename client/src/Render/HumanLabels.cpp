// HumanLabels.cpp — MEAMU: always-on name labels for HUMAN players.
//
// G (2026-11-13): "make it so i can always see HUMAN player labels".
//
// Stock behavior: a player's name appears above their head only while the
// mouse hovers them — RenderMonsterName (Render_WorldHelpers.cpp) calls
// CreateChat(name, "", entity, 0, -1) once per frame for the hovered entity,
// which keeps a name-only entry alive in the chat-bubble pool; RenderBooleans
// (HUD_Pass1.cpp) then projects and draws it. This pass extends exactly that
// mechanism to EVERY human player in the viewport: once per frame, right
// before RenderBooleans runs, it feeds the same CreateChat call for each
// qualifying entity. The hover state is not touched, and monsters / NPCs /
// summons keep stock behavior (this pass only accepts Kind == PlayerKind).
//
// HUMAN vs PHANTOM: the 100 Kage/villager phantoms are server-fabricated
// user objects — to the client they are indistinguishable from players
// (Kind 1, player models). They are excluded by NAME against the shipped
// list Data/Local/phantom_names.txt (generated from the roster source
// builds/konoha-pack-2026-11-12/roster/villagers.json by
// gen_phantom_names.py; regenerate it when the roster changes).
// Fail-safe direction: an unlisted name is treated as HUMAN (labeled), and
// if the file is missing the list is simply empty — everyone is labeled.
//
// Viewport bounding: CreateChat itself refuses entities that are not Live
// or not frustum-Visible, and UI_TickHoverBubbles (UI/Chat.cpp) zeroes a
// bubble entry as soon as its owner leaves the world or the view, so labels
// never linger off-screen or past a player's departure.
//
// The hero is included: he is a human player character in the viewport.
// (Flip kIncludeHero to exclude him; stock MU never shows the own name.)

#include "stdafx.h"
#include "Entity/EntityName.h"
#include "Entity/EntityTypes.h"
#include "Entity/EntityView.h"
#include "Render/PhantomNames.h"
#include "globals.h"
#include "functions.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

namespace {

constexpr bool kIncludeHero = true;

std::unordered_set<std::string> s_PhantomNames;
bool s_LoadAttempted = false;

std::string LowerAscii(const char* text)
{
    std::string out;
    if (!text) return out;
    for (const char* p = text; *p; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out += c;
    }
    return out;
}

void LoadPhantomNames()
{
    s_LoadAttempted = true;
    FILE* file = std::fopen("Data/Local/phantom_names.txt", "r");
    if (!file) return;  // fail-safe: empty list -> every player is labeled
    char line[128];
    while (std::fgets(line, sizeof(line), file)) {
        char* begin = line;
        while (*begin == ' ' || *begin == '\t') ++begin;
        char* end = begin + std::strlen(begin);
        while (end > begin && (end[-1] == '\n' || end[-1] == '\r' ||
                               end[-1] == ' ' || end[-1] == '\t'))
            --end;
        *end = '\0';
        if (!*begin || *begin == '#') continue;
        s_PhantomNames.insert(LowerAscii(begin));
    }
    std::fclose(file);
}

} // namespace

// The shared phantom check (Render/PhantomNames.h): the world
// labels below and the Tab map markers (UI/MiniMap.cpp) classify
// players through this one loader, so the tell never drifts
// between surfaces. Loads lazily on first use.
bool Phantom_IsPhantomName(const char* name)
{
    if (!s_LoadAttempted) LoadPhantomNames();
    if (!name || !name[0]) return false;
    return s_PhantomNames.find(LowerAscii(name)) != s_PhantomNames.end();
}

// Called from Render_GameFrame (Render_Frame.cpp) immediately before
// Render_FloatingText (RenderBooleans), so the entries fed here are
// projected and drawn in the same frame — the same cadence the hover path
// uses (RenderMonsterName feeds once per frame from the 3D pass).
void Render_HumanLabels(void)
{
    if (SceneFlag != 5) return;
    if (!s_LoadAttempted) LoadPhantomNames();
    if (!CharactersClient) return;

    const BYTE* base = (const BYTE*)(uintptr_t)CharactersClient;
    const BYTE* hero = (const BYTE*)(uintptr_t)DAT_07abf5d8;
    for (int i = 0; i < EntityView::Capacity; ++i) {
        const BYTE* ent = base + i * CHARACTER_STRIDE;
        if (!kIncludeHero && ent == hero) continue;
        const EntityView view(ent);
        if (!view.IsActive() || view.Kind() != EntityView::PlayerKind) continue;
        const char* name = view.Name();
        if (!name || !name[0]) continue;
        if (Phantom_IsPhantomName(name)) continue;
        // The exact call the hover path makes (Render_WorldHelpers.cpp):
        // empty text = name-only bubble; CreateChat keeps it refreshed.
        CreateChat((char*)name, (char*)"", AsCharacter((void*)ent), 0, -1);
    }
}
