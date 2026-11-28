// =====================================================================
// CONTROLLER MAPPER (MEAMU Train 100, 2026-11-21) -- see PadMap.h
// for the charter. The fetch rides the system curl (present on
// macOS and on Windows 10 1803+) with its config on a temp file,
// so the account password never appears on a command line. The
// response lands in a second temp file; both are deleted the
// moment the fetch resolves. Everything runs on a detached
// thread: login never waits on the network, and the map swaps in
// atomically whenever it lands (usually before character select
// is done). Any failure at any step keeps the Default preset.
// =====================================================================

#include "Input/PadMap.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace {

const char* kControlNames[PC_COUNT] = {
    "LT", "LB", "L3", "DUP", "DLEFT", "DRIGHT", "DDOWN",
    "BACK", "START",
    "RT", "RB", "Y", "X", "B", "A", "R3"
};

const char* kActionNames[PA_COUNT] = {
    "none", "dash", "teleport", "lock_toggle", "lock_pour",
    "skill1", "skill2", "skill3", "skill4", "skill5",
    "skill6", "skill7", "skill8", "skill9", "skill10",
    "pickup", "fishing", "scheme_toggle", "minimap"
};

// Tonight's final layout -- the Default preset, identical to the
// site's DEFAULT_BINDINGS (lib/controller-map.js).
// Train 138 (G's order, 2026-11-28): "hold X = skill 7, hold
// Y = skill 8, hold B = skill 9" -- the face buttons take the
// three high slots as hold-casts (every PA_SKILLn already rides
// the held digit mirror in PadButtonsTick, so holding the
// button repeat-casts at the skill's own cycle; release stops).
// Skills 3/4/5 move to the controls 7/8/9 vacated (BACK/L3/DUP)
// so all ten skills stay on the pad, one action per control.
// NOTE: the site's DEFAULT_BINDINGS must be updated to match
// (shared-contract law in PadMap.h) -- flagged to GG.
const PadAction kDefaultMap[PC_COUNT] = {
    /* LT */ PA_MINIMAP,   // Train 118 (G's order): LT is the whole-map overlay
    /* LB */ PA_LOCK_TOGGLE,
    /* L3 */ PA_SKILL4,    // Train 138: Skill 8 moved to Y; Skill 4 (Y's old) lands here
    /* DUP */ PA_SKILL5,   // Train 138: Skill 9 moved to B; Skill 5 (B's old) lands here
    /* DLEFT */ PA_FISHING,
    /* DRIGHT */ PA_TELEPORT,   // Train 118: Teleport moves here -- LT's old tenant, still one press away
    /* DDOWN */ PA_SKILL10,
    /* BACK */ PA_SKILL3,  // Train 138: Skill 7 moved to X; Skill 3 (X's old) lands here
    /* START */ PA_SCHEME_TOGGLE,
    /* RT */ PA_LOCK_POUR,
    /* RB */ PA_SKILL6,
    /* Y */ PA_SKILL8,     // Train 138 (G's order): hold Y = Skill 8
    /* X */ PA_SKILL7,     // Train 138 (G's order): hold X = Skill 7
    /* B */ PA_SKILL9,     // Train 138 (G's order): hold B = Skill 9
    /* A */ PA_DASH,
    /* R3 */ PA_PICKUP
};

PadAction gActiveMap[PC_COUNT];
std::once_flag gInitOnce;
std::atomic<bool> gAccountMapActive{false};
std::mutex gFetchMutex;
std::string gFetchedAccount;

void InitOnce() {
    std::memcpy(gActiveMap, kDefaultMap, sizeof(gActiveMap));
}

int FindName(const char* const* names, int count, const std::string& s) {
    for (int i = 0; i < count; i++) {
        if (s == names[i]) return i;
    }
    return -1;
}

// Minimal extractor for the fetch response: find "bindings",
// then read its "KEY" : "value" string pairs. Anything unknown
// is ignored; a pair that would double-book an action is
// dropped (first control in contract order wins), mirroring
// the site's validator.
bool ParseBindings(const std::string& json, PadAction out[PC_COUNT]) {
    const size_t anchor = json.find("\"bindings\"");
    if (anchor == std::string::npos) return false;
    const size_t open = json.find('{', anchor);
    if (open == std::string::npos) return false;

    bool seen[PC_COUNT] = {};
    bool actionUsed[PA_COUNT] = {};
    PadAction parsed[PC_COUNT];
    for (int i = 0; i < PC_COUNT; i++) parsed[i] = PA_NONE;

    size_t p = open + 1;
    while (p < json.size()) {
        const size_t q1 = json.find('"', p);
        if (q1 == std::string::npos) break;
        const size_t q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        const std::string key = json.substr(q1 + 1, q2 - q1 - 1);
        const size_t colon = json.find(':', q2);
        if (colon == std::string::npos) break;
        const size_t v1 = json.find('"', colon);
        if (v1 == std::string::npos) break;
        const size_t v2 = json.find('"', v1 + 1);
        if (v2 == std::string::npos) break;
        const std::string val = json.substr(v1 + 1, v2 - v1 - 1);
        p = v2 + 1;

        const int ci = FindName(kControlNames, PC_COUNT, key);
        const int ai = FindName(kActionNames, PA_COUNT, val);
        if (ci < 0 || ai < 0 || seen[ci]) continue;
        seen[ci] = true;
        if (ai != PA_NONE && actionUsed[ai]) continue; // stays none
        if (ai != PA_NONE) actionUsed[ai] = true;
        parsed[ci] = (PadAction)ai;
        if (json.find('}', p) != std::string::npos && json.find('"', p) > json.find('}', p)) break;
    }

    bool any = false;
    for (int i = 0; i < PC_COUNT; i++) any = any || seen[i];
    if (!any) return false;
    std::memcpy(out, parsed, sizeof(parsed));
    return true;
}

std::string JsonEscape(const char* s) {
    std::string out;
    for (const char* p = s; *p; p++) {
        if (*p == '"' || *p == '\\') { out += '\\'; out += *p; }
        else if ((unsigned char)*p >= 0x20) out += *p;
    }
    return out;
}

void FetchThread(std::string account, std::string password) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec);
    if (ec) return;
    const std::string tag = std::to_string((unsigned long long)
        std::hash<std::string>{}(account) & 0xFFFFFF);
    const fs::path cfgPath = dir / ("meamu-padmap-" + tag + ".cfg");
    const fs::path outPath = dir / ("meamu-padmap-" + tag + ".json");

    {
        std::ofstream cfg(cfgPath, std::ios::trunc);
        if (!cfg) return;
        cfg << "url = \"https://meamu.ca/api/controller-map/fetch\"\n";
        cfg << "header = \"Content-Type: application/json\"\n";
        cfg << "data = \"{\\\"account\\\":\\\"" << JsonEscape(account.c_str())
            << "\\\",\\\"password\\\":\\\"" << JsonEscape(password.c_str()) << "\\\"}\"\n";
        cfg << "output = \"" << outPath.string() << "\"\n";
        cfg << "silent\nmax-time = 8\n";
    }

    std::string cmd = "curl -s -K \"";
    cmd += cfgPath.string();
    cmd += "\"";
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (pipe) {
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
    }
    fs::remove(cfgPath, ec);

    std::ifstream in(outPath);
    if (in) {
        std::stringstream ss;
        ss << in.rdbuf();
        in.close();
        PadAction parsed[PC_COUNT];
        if (ParseBindings(ss.str(), parsed)) {
            std::lock_guard<std::mutex> lock(gFetchMutex);
            std::memcpy(gActiveMap, parsed, sizeof(gActiveMap));
            gAccountMapActive = true;
        }
    }
    fs::remove(outPath, ec);
}

} // namespace

PadAction PadMap_ActionFor(int control) {
    std::call_once(gInitOnce, InitOnce);
    if (control < 0 || control >= PC_COUNT) return PA_NONE;
    return gActiveMap[control];
}

int PadMap_ControlFor(int action) {
    std::call_once(gInitOnce, InitOnce);
    if (action <= PA_NONE || action >= PA_COUNT) return -1;
    for (int i = 0; i < PC_COUNT; i++) {
        if (gActiveMap[i] == action) return i;
    }
    return -1;
}

bool PadMap_AccountMapActive() {
    return gAccountMapActive.load();
}

void PadMap_OnLogin(const char* account, const char* password) {
    if (!account || !*account || !password) return;
    {
        std::lock_guard<std::mutex> lock(gFetchMutex);
        if (gFetchedAccount == account) return; // once per account per run
        gFetchedAccount = account;
    }
    std::thread(FetchThread, std::string(account), std::string(password)).detach();
}
