// AutoLogin.cpp -- TRAIN 116 (2026-11-23). See AutoLogin.h for the contract.
//
// State machine (one shot per launch; any failure disarms it for the run and
// leaves the stock manual flow untouched):
//   WAIT_SERVER_LIST -> (server entry 0 picked, stock click tail) ->
//   WAIT_JOIN        -> (F1/00 hook sends the login) ->
//   WAIT_CHAR_LIST   -> (named slot found -> char-select entry state) -> DONE
#include "stdafx.h"
#include "Net/AutoLogin.h"
#include "Net/Network.h"
#include "Net/Reconnect.h"
#include "Entity/EntityName.h"
#include "Entity/EntityTypes.h"
#include "Platform/ITimer.h"
#include <cstdio>
#include <cstring>
#include <cctype>

extern "C" void DbgLogPublic(const char* msg);
extern void CreateSocket(const char *server, unsigned int port);

namespace {

struct AutoLoginConfig {
    bool enabled = false;
    char account[16] = {};
    char password[16] = {};
    char character[64] = {};
};

enum Stage { STAGE_OFF, STAGE_WAIT_SERVER_LIST, STAGE_WAIT_JOIN, STAGE_WAIT_CHAR_LIST, STAGE_DONE };

Stage g_Stage = STAGE_OFF;
bool  g_Loaded = false;
bool  g_LoginSent = false;
bool  g_GroupSent = false;
DWORD g_StartTick = 0;
DWORD g_ListSeenTick = 0;
AutoLoginConfig g_Cfg;

char* Trim(char* s)
{
    while (*s && std::isspace((unsigned char)*s)) s++;
    char* end = s + std::strlen(s);
    while (end > s && std::isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

void LoadConfig()
{
    g_Loaded = true;
    FILE* f = std::fopen("autologin.ini", "r");
    if (!f) return;
    char line[256];
    bool inSection = false;
    while (std::fgets(line, sizeof(line), f)) {
        char* t = Trim(line);
        if (*t == '[') { inSection = (std::strncmp(t, "[AutoLogin]", 11) == 0); continue; }
        if (!inSection) continue;
        char* eq = std::strchr(t, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = Trim(t);
        char* val = Trim(eq + 1);
        if (std::strcmp(key, "Enabled") == 0) g_Cfg.enabled = (std::atoi(val) != 0);
        else if (std::strcmp(key, "Account") == 0) std::strncpy(g_Cfg.account, val, sizeof(g_Cfg.account) - 1);
        else if (std::strcmp(key, "Password") == 0) std::strncpy(g_Cfg.password, val, sizeof(g_Cfg.password) - 1);
        else if (std::strcmp(key, "Character") == 0) std::strncpy(g_Cfg.character, val, sizeof(g_Cfg.character) - 1);
    }
    std::fclose(f);
    if (g_Cfg.enabled && g_Cfg.account[0] && g_Cfg.password[0] && g_Cfg.character[0]) {
        g_Stage = STAGE_WAIT_SERVER_LIST;
        g_StartTick = Platform::GetTimer().Ticks();
        DbgLogPublic("AutoLogin: armed (autologin.ini) -- hands-free login engaged");
    } else if (g_Cfg.enabled) {
        DbgLogPublic("AutoLogin: Enabled=1 but Account/Password/Character incomplete -- staying manual");
    }
}

// The server-select click tail (Scene_ServerSelect_Input) for entry (0,0),
// minus the sound/log cosmetics: same globals, same packets, same state.
void PickFirstServer()
{
    ServerSelectLo = 0;
    unsigned short serverCode = *(unsigned short*)((char*)&DAT_083a4604 + (int)ServerSelectHi * 0x21e);
    ServerLocalSelect = (unsigned int)serverCode % 0x14 + 1;
    DAT_083a7c44 = 0x32;
    DAT_083a7c24 = DAT_083a7c28;
    DAT_083a7c28 = 0;
    DAT_083a7ac8 = Platform::GetTimer().Ticks();
    DAT_083a4320 = 0;
    DAT_083a4328 = (DAT_083a4328 & 0xffff0000) | serverCode;
    if (gNetwork.IsConnectServerMode()) {
        BYTE pkt6[6] = { 0xC1, 0x06, 0xF4, 0x03,
                         (BYTE)(serverCode & 0xff), (BYTE)(serverCode >> 8) };
        gNetwork.SendToConnectServer(pkt6, 6);
    } else {
        CreateSocket(PTR_s_connect_muonline_co_kr_005615b8, (unsigned int)g_ServerPort);
    }
    DAT_083a7c14 = 1;   // state = Connecting
    g_Stage = STAGE_WAIT_JOIN;
    DbgLogPublic("AutoLogin: server picked, connecting");
}

bool FindCharacterSlot()
{
    for (int slot = 0; slot < 5; slot++) {
        const void* entry = (const void*)(std::uintptr_t)(CharactersClient + slot * CHARACTER_STRIDE);
        const char* name = EntityName::Get(entry);
        if (name && name[0] && std::strcmp(name, g_Cfg.character) == 0) {
            SelectedHero = (DWORD)slot;
            return true;
        }
    }
    return false;
}

bool AnyCharacterListed()
{
    for (int slot = 0; slot < 5; slot++) {
        const void* entry = (const void*)(std::uintptr_t)(CharactersClient + slot * CHARACTER_STRIDE);
        const char* name = EntityName::Get(entry);
        if (name && name[0]) return true;
    }
    return false;
}

} // namespace

void AutoLogin_OnJoinServer()
{
    if (!g_Loaded) LoadConfig();
    if (g_Stage != STAGE_WAIT_JOIN || g_LoginSent) return;
    // Mirror the stock login block (Game_SceneUpdate): stage the ID, flag the
    // attempt, then the shared send path (SendLogin + reconnect bookkeeping).
    std::strncpy(LogInID, g_Cfg.account, sizeof(LogInID) - 1);
    LogIn = 1;
    gNetwork.SendLogin(g_Cfg.account, g_Cfg.password);
    gReconnect.OnLoginSent(g_Cfg.account, g_Cfg.password);
    g_LoginSent = true;
    g_Stage = STAGE_WAIT_CHAR_LIST;
    DbgLogPublic("AutoLogin: login sent");
}

void AutoLogin_Tick()
{
    if (!g_Loaded) LoadConfig();
    if (g_Stage == STAGE_OFF || g_Stage == STAGE_DONE) return;
    const DWORD now = Platform::GetTimer().Ticks();

    if (g_Stage == STAGE_WAIT_SERVER_LIST) {
        if (SceneFlag != 2 || DAT_083a7c14 != 0) return;
        if (now - g_StartTick <= 1500) return;   // let the title settle
        // Step A: no group chosen yet -- replicate the group click: the
        // F4/02 request is what makes the ConnectServer send the channel
        // list; without it the channel array stays zeroed forever.
        if (!g_GroupSent) {
            if (gNetwork.IsConnectServerMode()) {
                BYTE pkt4[4] = { 0xC1, 0x04, 0xF4, 0x02 };
                gNetwork.SendToConnectServer(pkt4, 4);
            }
            ServerSelectLo = -1;
            g_GroupSent = true;
            DbgLogPublic("AutoLogin: group picked, list requested");
            return;
        }
        // Step B: the F4/02 response files each group into display
        // slot 23 - (ServerCode/20) (Recv_ServerList) -- group 0 lands
        // in slot 23, NOT slot 0. Scan the slots like the eye would:
        // the first slot whose channel count (+0x14 in its header)
        // is populated is the group to enter; channel 0's code may
        // legitimately be 0 (code % 20 == 0 = channel 1), so the
        // count -- not the code -- is the populated signal, and the
        // load byte at code+2 carries the full flag in bit 7.
        for (int slot = 0; slot < 25; slot++) {
            const int grp = slot * 0x21e;
            const unsigned char numCh = *(unsigned char*)((char*)&DAT_083a4604 - 0x18 + grp);
            if (numCh == 0) continue;
            const unsigned char load = *(unsigned char*)((char*)&DAT_083a4606 + grp);
            if ((load & 0x80) != 0 || DAT_083a7c44 != 0) return;
            ServerSelectHi = slot;
            PickFirstServer();
            return;
        }
        return;
    }

    if (g_Stage == STAGE_WAIT_JOIN) {
        // The F1/00 hook does the send. A login failure lands the scene in
        // sub-state 4 (error panel) -- disarm and leave it manual.
        if (g_LoginSent == false && now - g_StartTick > 30000) {
            g_Stage = STAGE_OFF;
            DbgLogPublic("AutoLogin: no JoinServer in 30 s -- disarmed, manual login");
        }
        return;
    }

    if (g_Stage == STAGE_WAIT_CHAR_LIST) {
        if (DAT_083a7c14 == 4 && g_LoginSent) {
            g_Stage = STAGE_OFF;
            DbgLogPublic("AutoLogin: login failed -- disarmed, manual login");
            return;
        }
        // Char-select scene up and the list received: booth-proven
        // gate values (2026-11-23) are SceneFlag 4 with the protocol
        // state at 0x33 -- the original pair; the stage only ever
        // failed because the tick was hooked into a scene-2 function.
        if (SceneFlag == 4 && DAT_05826cb0 >= 0x33) {
            if (FindCharacterSlot()) {
                DAT_083a7c14 = 0x19;   // char-select entry state (stock case 0x19 completes it)
                g_Stage = STAGE_DONE;
                DbgLogPublic("AutoLogin: character selected, entering world");
                return;
            }
            if (AnyCharacterListed()) {
                if (g_ListSeenTick == 0) g_ListSeenTick = now;
                if (now - g_ListSeenTick > 3000) {
                    g_Stage = STAGE_OFF;
                    DbgLogPublic("AutoLogin: configured character not in the list -- disarmed, pick manually");
                }
            }
        }
        return;
    }
}
