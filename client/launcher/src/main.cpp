// MEAMU Launcher — auto-updating starter for the MEAMU desktop client.
//
// Drop MEAMU-Launcher.exe next to main.exe and run it instead of main.exe.
// It reads the local client's version from the stamp embedded in main.exe
// (vYYYYMMDD-HHMM), fetches the live version config from meamu.ca,
// downloads the update pack (or the full client when main.exe is missing),
// verifies the new exe's stamp BEFORE swapping it in, then launches the game.
//
// Failure rule: any network/download problem leaves the current install
// untouched and offers "Play anyway" on the version already on disk.
//
// Single-file Win32 app: WinHTTP for downloads, vendored miniz (public
// domain / MIT) for zip extraction. No runtime dependencies beyond Windows
// itself (the CI build statically links the MSVC CRT).

#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "miniz.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")
#if defined(_MSC_VER)
// Visual styles for the progress bar (MSVC only; ignored by other toolchains).
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const wchar_t* kSiteHost = L"meamu.ca";
// The site's live version config (same fields as config/downloads.json on the
// server), published as a static file inside the downloads dir. The bare
// /downloads.json path is the fallback if it is ever exposed directly.
const wchar_t* kConfigPaths[] = {L"/downloads/version.json", L"/downloads.json"};
const wchar_t* kUserAgent = L"MEAMU-Launcher/1.0";

const char* kClientDirPrefix = "MEAMU-Windows/";  // arc root inside every zip
const char* kPackReadme = "UPDATE-README.txt";    // skipped on extraction

// Window messages from the worker thread.
const UINT kMsgStatus = WM_APP + 1;    // lParam = std::wstring* (owned)
const UINT kMsgProgress = WM_APP + 2;  // wParam = permille 0..1000, -1 = marquee
const UINT kMsgState = WM_APP + 3;     // wParam = State
const UINT kMsgAutoLaunch = WM_APP + 4;

enum State {
  kWorking = 0,      // busy; Play disabled
  kPlayReady = 1,    // Play enabled, normal label
  kPlayAnyway = 2,   // Play enabled, "Play anyway" label (something failed)
};

COLORREF kColBg = RGB(0x08, 0x09, 0x0b);      // charcoal
COLORREF kColGold = RGB(0xd8, 0xa9, 0x3b);    // accent
COLORREF kColGreen = RGB(0x78, 0xdc, 0x3c);   // Senju green (ready)
COLORREF kColText = RGB(0xe8, 0xe6, 0xe1);
COLORREF kColDim = RGB(0x9a, 0x97, 0x90);

// ---------------------------------------------------------------------------
// Globals (UI thread owns the controls; worker posts messages)
// ---------------------------------------------------------------------------

HWND g_wnd = nullptr;
HWND g_title = nullptr;
HWND g_sub = nullptr;
HWND g_status = nullptr;
HWND g_versions = nullptr;
HWND g_bar = nullptr;
HWND g_play = nullptr;
HBRUSH g_bgBrush = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_btnFont = nullptr;
HFONT g_bodyFont = nullptr;
HANDLE g_worker = nullptr;
std::atomic<bool> g_cancel{false};
std::atomic<bool> g_playEnabled{false};
std::wstring g_gameDir;  // folder the launcher (and main.exe) lives in

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
  return out;
}

std::string WideToUtf8(const std::wstring& s) {
  if (s.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
  return out;
}

bool FileExists(const std::wstring& path) {
  DWORD a = GetFileAttributesW(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring DirOf(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool EnsureDir(const std::wstring& path) {
  if (path.empty()) return false;
  DWORD a = GetFileAttributesW(path.c_str());
  if (a != INVALID_FILE_ATTRIBUTES) return (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
  size_t slash = path.find_last_of(L"\\/");
  if (slash != std::wstring::npos) EnsureDir(path.substr(0, slash));
  return CreateDirectoryW(path.c_str(), nullptr) != 0 ||
         GetLastError() == ERROR_ALREADY_EXISTS;
}

// Creates every parent directory of a file path (relative to nothing; the
// path is absolute by the time we get here).
void EnsureParentDirs(const std::wstring& filePath) {
  std::wstring dir = DirOf(filePath);
  if (!dir.empty()) EnsureDir(dir);
}

// ---------------------------------------------------------------------------
// Version stamp scan — finds "vYYYYMMDD-HHMM" embedded in a binary, the same
// stamp the ship finishers verify with a substring check. Validates the
// fields so random version-looking bytes don't count.
// ---------------------------------------------------------------------------

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

bool StampAt(const char* p) {
  // p points at 'v'; needs 14 chars: v YYYYMMDD - HHMM
  if (p[0] != 'v' || p[9] != '-') return false;
  for (int i = 1; i <= 8; ++i) if (!IsDigit(p[i])) return false;
  for (int i = 10; i <= 13; ++i) if (!IsDigit(p[i])) return false;
  int month = (p[5] - '0') * 10 + (p[6] - '0');
  int day = (p[7] - '0') * 10 + (p[8] - '0');
  int hour = (p[10] - '0') * 10 + (p[11] - '0');
  int minute = (p[12] - '0') * 10 + (p[13] - '0');
  return month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour <= 23 &&
         minute <= 59;
}

// Returns the stamp (e.g. "v20261116-0012") or "" when none is found.
std::string ScanStamp(const std::wstring& filePath) {
  HANDLE h = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return "";
  std::string result;
  const size_t kChunk = 1 << 20;
  std::vector<char> buf(kChunk + 16);
  size_t carry = 0;  // tail bytes kept so a stamp split across chunks is seen
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(h, buf.data() + carry, (DWORD)kChunk, &read, nullptr) || read == 0)
      break;
    size_t total = carry + read;
    if (total >= 14) {
      for (size_t i = 0; i + 14 <= total; ++i) {
        if (buf[i] == 'v' && StampAt(buf.data() + i)) {
          result.assign(buf.data() + i, 14);
          break;
        }
      }
    }
    if (!result.empty()) break;
    carry = total < 13 ? total : 13;
    memmove(buf.data(), buf.data() + total - carry, carry);
    if (read < kChunk) break;
  }
  CloseHandle(h);
  return result;
}

// ---------------------------------------------------------------------------
// Minimal JSON string-field extractor — the version config is a small flat
// object ("patchVersion": "v...", "patchUrl": "/...", ...). Finds the first
// string value for a key; not a general JSON parser.
// ---------------------------------------------------------------------------

std::string JsonStringField(const std::string& json, const std::string& key) {
  std::string needle = "\"" + key + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return "";
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return "";
  pos = json.find('"', pos + 1);
  if (pos == std::string::npos) return "";
  std::string out;
  for (size_t i = pos + 1; i < json.size(); ++i) {
    char c = json[i];
    if (c == '\\' && i + 1 < json.size()) {
      out += json[i + 1];
      ++i;
    } else if (c == '"') {
      return out;
    } else {
      out += c;
    }
  }
  return "";
}

// ---------------------------------------------------------------------------
// WinHTTP helpers (worker thread only)
// ---------------------------------------------------------------------------

struct HttpResult {
  bool ok = false;
  DWORD status = 0;
  std::string error;
};

HINTERNET OpenSession() {
  HINTERNET s = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (s) WinHttpSetTimeouts(s, 15000, 15000, 30000, 30000);
  return s;
}

// Splits a root-relative path ("/downloads/x.zip") or absolute URL into a
// request path on kSiteHost. Only our own host is ever contacted.
std::wstring RequestPath(const std::string& urlOrPath) {
  std::wstring w = Utf8ToWide(urlOrPath);
  const std::wstring prefix = L"https://meamu.ca";
  if (w.compare(0, prefix.size(), prefix) == 0) return w.substr(prefix.size());
  if (!w.empty() && w[0] == L'/') return w;
  return L"/" + w;
}

// GETs a small text body (the version config).
HttpResult HttpGetText(const std::wstring& path, std::string* body) {
  HttpResult r;
  HINTERNET session = OpenSession();
  if (!session) { r.error = "no network stack"; return r; }
  HINTERNET conn = WinHttpConnect(session, kSiteHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE)
                       : nullptr;
  if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(req, nullptr)) {
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, nullptr);
    r.status = status;
    if (status == 200) {
      std::string data;
      for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (avail == 0) { r.ok = true; break; }
        std::vector<char> chunk(avail);
        DWORD got = 0;
        if (!WinHttpReadData(req, chunk.data(), avail, &got)) break;
        data.append(chunk.data(), got);
      }
      if (r.ok) *body = data;
    }
  }
  if (!r.ok && r.error.empty()) r.error = "request failed";
  if (req) WinHttpCloseHandle(req);
  if (conn) WinHttpCloseHandle(conn);
  WinHttpCloseHandle(session);
  return r;
}

void PostStatus(const std::wstring& text);
void PostProgress(long permille);

// Downloads a file with progress posts. Returns false on any failure; the
// partial file is left for the caller to delete.
bool HttpDownloadToFile(const std::wstring& path, const std::wstring& destPath) {
  HINTERNET session = OpenSession();
  if (!session) return false;
  bool ok = false;
  HINTERNET conn = WinHttpConnect(session, kSiteHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE)
                       : nullptr;
  HANDLE file = INVALID_HANDLE_VALUE;
  if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(req, nullptr)) {
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, nullptr);
    if (status == 200) {
      DWORD totalLen = 0;
      len = sizeof(totalLen);
      bool haveTotal = WinHttpQueryHeaders(
                           req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &totalLen, &len, nullptr) != 0;
      file = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file != INVALID_HANDLE_VALUE) {
        ok = true;
        unsigned long long done = 0;
        std::vector<char> chunk(1 << 16);
        while (!g_cancel.load()) {
          DWORD avail = 0;
          if (!WinHttpQueryDataAvailable(req, &avail)) { ok = false; break; }
          if (avail == 0) break;
          DWORD want = avail < (DWORD)chunk.size() ? avail : (DWORD)chunk.size();
          DWORD got = 0;
          if (!WinHttpReadData(req, chunk.data(), want, &got) || got == 0) {
            ok = false;
            break;
          }
          DWORD written = 0;
          if (!WriteFile(file, chunk.data(), got, &written, nullptr) ||
              written != got) {
            ok = false;
            break;
          }
          done += got;
          PostProgress(haveTotal && totalLen > 0
                           ? (long)(done * 1000 / totalLen)
                           : -1);
        }
        if (g_cancel.load()) ok = false;
      }
    }
  }
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  if (req) WinHttpCloseHandle(req);
  if (conn) WinHttpCloseHandle(conn);
  WinHttpCloseHandle(session);
  return ok;
}

// ---------------------------------------------------------------------------
// Zip extraction via miniz. Every entry lives under "MEAMU-Windows/" inside
// the shipped zips; that prefix is stripped so files land in the game folder.
// Returns false on a corrupt/unreadable archive.
// ---------------------------------------------------------------------------

std::string NormalizeEntry(const char* name) {
  std::string n = name ? name : "";
  for (auto& c : n) if (c == '\\') c = '/';
  const std::string prefix = kClientDirPrefix;
  if (n.compare(0, prefix.size(), prefix) == 0) n = n.substr(prefix.size());
  return n;
}

bool ExtractZip(const std::wstring& zipPath, const std::wstring& destDir,
                const std::string& skipEntry) {
  std::string zipUtf8 = WideToUtf8(zipPath);
  mz_zip_archive zip;
  memset(&zip, 0, sizeof(zip));
  if (!mz_zip_reader_init_file(&zip, zipUtf8.c_str(), 0)) return false;
  bool ok = true;
  mz_uint count = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < count && ok; ++i) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, i, &st)) { ok = false; break; }
    std::string rel = NormalizeEntry(st.m_filename);
    if (rel.empty() || rel == skipEntry || rel == kPackReadme) continue;
    if (rel.find("..") != std::string::npos) { ok = false; break; }  // no escapes
    std::wstring dest = destDir + L"\\" + Utf8ToWide(rel);
    if (mz_zip_reader_is_file_a_directory(&zip, i)) {
      EnsureDir(dest);
      continue;
    }
    EnsureParentDirs(dest);
    std::string destUtf8 = WideToUtf8(dest);
    if (!mz_zip_reader_extract_to_file(&zip, i, destUtf8.c_str(), 0)) ok = false;
  }
  mz_zip_reader_end(&zip);
  return ok;
}

// Extracts just main.exe from a zip into destDir (as destDir\main.exe).
bool ExtractMainExe(const std::wstring& zipPath, const std::wstring& destDir) {
  std::string zipUtf8 = WideToUtf8(zipPath);
  mz_zip_archive zip;
  memset(&zip, 0, sizeof(zip));
  if (!mz_zip_reader_init_file(&zip, zipUtf8.c_str(), 0)) return false;
  bool ok = false;
  mz_uint count = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < count; ++i) {
    char name[512];
    if (!mz_zip_reader_get_filename(&zip, i, name, sizeof(name))) continue;
    if (NormalizeEntry(name) == "main.exe") {
      std::wstring dest = destDir + L"\\main.exe";
      ok = mz_zip_reader_extract_to_file(&zip, i, WideToUtf8(dest).c_str(), 0) != 0;
      break;
    }
  }
  mz_zip_reader_end(&zip);
  return ok;
}


// ---------------------------------------------------------------------------
// UI-thread messaging
// ---------------------------------------------------------------------------

void PostStatus(const std::wstring& text) {
  if (g_wnd) PostMessageW(g_wnd, kMsgStatus, 0,
                          reinterpret_cast<LPARAM>(new std::wstring(text)));
}

void PostProgress(long permille) {
  if (g_wnd) PostMessageW(g_wnd, kMsgProgress, (WPARAM)permille, 0);
}

void PostState(State s) {
  if (g_wnd) PostMessageW(g_wnd, kMsgState, (WPARAM)s, 0);
}

void SetVersionsLine(const std::string& local, const std::string& remote) {
  std::wstring line = L"Your version: " +
                      (local.empty() ? std::wstring(L"none") : Utf8ToWide(local)) +
                      L"    Latest: " +
                      (remote.empty() ? std::wstring(L"?") : Utf8ToWide(remote));
  if (g_wnd) PostMessageW(g_wnd, kMsgStatus, 1,
                          reinterpret_cast<LPARAM>(new std::wstring(line)));
}

// ---------------------------------------------------------------------------
// Game launch
// ---------------------------------------------------------------------------

bool LaunchGame() {
  std::wstring exe = g_gameDir + L"\\main.exe";
  std::wstring cmd = L"\"" + exe + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0,
                           nullptr, g_gameDir.c_str(), &si, &pi);
  if (ok) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  return ok != 0;
}

// ---------------------------------------------------------------------------
// Update flow (worker thread)
// ---------------------------------------------------------------------------

void FailWithPlayAnyway(const std::wstring& message) {
  PostProgress(0);
  PostStatus(message);
  PostState(FileExists(g_gameDir + L"\\main.exe") ? kPlayAnyway : kWorking);
}

// Applies a downloaded update pack: stages the new exe, verifies its stamp,
// copies any other changed files, and only then swaps the exe. The old exe
// is never touched until the new one is verified on disk.
bool ApplyUpdatePack(const std::wstring& zipPath, const std::string& remoteStamp,
                     const std::string& localStamp) {
  std::wstring stageDir = g_gameDir + L"\\.meamu-stage";
  EnsureDir(stageDir);
  std::wstring stagedExe = stageDir + L"\\main.exe";
  DeleteFileW(stagedExe.c_str());

  PostStatus(L"Unpacking update...");
  PostProgress(-1);
  if (!ExtractMainExe(zipPath, stageDir) || !FileExists(stagedExe)) {
    FailWithPlayAnyway(L"The update pack was damaged. Playing the current version.");
    return false;
  }
  std::string newStamp = ScanStamp(stagedExe);
  if (newStamp != remoteStamp) {
    DeleteFileW(stagedExe.c_str());
    FailWithPlayAnyway(L"The update didn't check out, so nothing was changed. " \
                       L"Playing the current version.");
    return false;
  }
  // Any other files in the pack (data rounds) land next; the exe swaps last.
  ExtractZip(zipPath, g_gameDir, "main.exe");

  std::wstring liveExe = g_gameDir + L"\\main.exe";
  std::wstring backup = liveExe + L".bak-" +
                        Utf8ToWide(localStamp.empty() ? "unknown" : localStamp);
  DeleteFileW(backup.c_str());
  if (!MoveFileW(liveExe.c_str(), backup.c_str())) {
    FailWithPlayAnyway(L"Couldn't replace the old client (is the game running?). " \
                       L"Nothing was changed.");
    return false;
  }
  if (!MoveFileW(stagedExe.c_str(), liveExe.c_str())) {
    MoveFileW(backup.c_str(), liveExe.c_str());  // restore — never strand the folder
    FailWithPlayAnyway(L"The update couldn't be installed. Your old client was restored.");
    return false;
  }
  RemoveDirectoryW(stageDir.c_str());
  return true;
}

DWORD WINAPI WorkerProc(LPVOID) {
  std::wstring mainExe = g_gameDir + L"\\main.exe";
  bool haveExe = FileExists(mainExe);
  std::string localStamp = haveExe ? ScanStamp(mainExe) : "";

  PostStatus(L"Checking for updates...");
  std::string configJson;
  HttpResult cfg;
  for (const wchar_t* path : kConfigPaths) {
    cfg = HttpGetText(path, &configJson);
    if (cfg.ok) break;
  }
  if (!cfg.ok) {
    SetVersionsLine(localStamp, "");
    FailWithPlayAnyway(L"Can't reach the MEAMU server. Check your connection, " \
                       L"or play the version you have.");
    return 0;
  }

  std::string remoteStamp = JsonStringField(configJson, "patchVersion");
  std::string patchUrl = JsonStringField(configJson, "patchUrl");
  std::string clientUrl = JsonStringField(configJson, "clientUrl");
  if (remoteStamp.empty() || patchUrl.empty() || clientUrl.empty()) {
    SetVersionsLine(localStamp, "");
    FailWithPlayAnyway(L"The server's version info looks wrong. " \
                       L"Playing the version you have.");
    return 0;
  }
  SetVersionsLine(localStamp, remoteStamp);

  // Fresh install: no client yet — pull the full zip and unpack it here.
  if (!haveExe) {
    std::wstring zipPath = g_gameDir + L"\\meamu-client-download.zip";
    PostStatus(L"Downloading the full client (about 180 MB)...");
    if (!HttpDownloadToFile(RequestPath(clientUrl), zipPath)) {
      DeleteFileW(zipPath.c_str());
      FailWithPlayAnyway(L"The client download failed. Check your connection " \
                         L"and run the launcher again.");
      return 0;
    }
    PostStatus(L"Unpacking the client...");
    PostProgress(-1);
    bool extracted = ExtractZip(zipPath, g_gameDir, "");
    DeleteFileW(zipPath.c_str());
    PostProgress(0);
    if (!extracted || !FileExists(mainExe)) {
      FailWithPlayAnyway(L"The client couldn't be unpacked. Run the launcher again.");
      return 0;
    }
    PostStatus(L"Installed " + Utf8ToWide(remoteStamp) + L". Press Play!");
    PostState(kPlayReady);
    return 0;
  }

  // Already current — start the game right away.
  if (localStamp == remoteStamp) {
    PostProgress(1000);
    PostStatus(L"Up to date. Starting the game...");
    PostState(kPlayReady);
    Sleep(1000);
    if (!g_cancel.load()) PostMessageW(g_wnd, kMsgAutoLaunch, 0, 0);
    return 0;
  }

  // Update available.
  PostStatus(L"Update available: " + Utf8ToWide(localStamp) + L" -> " +
             Utf8ToWide(remoteStamp) + L". Downloading...");
  std::wstring zipPath = g_gameDir + L"\\meamu-update-download.zip";
  if (!HttpDownloadToFile(RequestPath(patchUrl), zipPath)) {
    DeleteFileW(zipPath.c_str());
    FailWithPlayAnyway(L"The update download failed. " \
                       L"Playing the current version.");
    return 0;
  }
  bool applied = ApplyUpdatePack(zipPath, remoteStamp, localStamp);
  DeleteFileW(zipPath.c_str());
  if (applied) {
    PostProgress(1000);
    // A fresh update waits for the Play click so the player sees it landed.
    PostStatus(L"Updated to " + Utf8ToWide(remoteStamp) + L". Press Play!");
    PostState(kPlayReady);
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

void StartWorker() {
  g_worker = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
}

void OnPlayClicked() {
  if (!g_playEnabled.load()) return;
  if (LaunchGame()) {
    DestroyWindow(g_wnd);
  } else {
    SetWindowTextW(g_status, L"Couldn't start main.exe — is it in this folder?");
  }
}

LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g_status = CreateWindowW(L"STATIC", L"Starting...",
                               WS_CHILD | WS_VISIBLE | SS_LEFT,
                               24, 118, 412, 44, wnd, nullptr, nullptr, nullptr);
      g_versions = CreateWindowW(L"STATIC", L"",
                                 WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 24, 96, 412, 18, wnd, nullptr, nullptr, nullptr);
      g_bar = CreateWindowW(PROGRESS_CLASSW, nullptr,
                            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                            24, 168, 412, 14, wnd, nullptr, nullptr, nullptr);
      SendMessageW(g_bar, PBM_SETRANGE32, 0, 1000);
      SendMessageW(g_bar, PBM_SETBARCOLOR, 0, (LPARAM)kColGreen);
      SendMessageW(g_bar, PBM_SETBKCOLOR, 0, (LPARAM)RGB(0x1c, 0x1e, 0x22));
      g_play = CreateWindowW(L"BUTTON", L"PLAY",
                             WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                             130, 202, 200, 46, wnd, (HMENU)1, nullptr, nullptr);
      EnableWindow(g_play, FALSE);
      SendMessageW(g_status, WM_SETFONT, (WPARAM)g_bodyFont, TRUE);
      SendMessageW(g_versions, WM_SETFONT, (WPARAM)g_bodyFont, TRUE);
      SendMessageW(g_play, WM_SETFONT, (WPARAM)g_btnFont, TRUE);
      StartWorker();
      return 0;
    }
    case WM_CTLCOLORSTATIC: {
      HDC dc = (HDC)wp;
      SetBkColor(dc, kColBg);
      HWND ctl = (HWND)lp;
      SetTextColor(dc, ctl == g_title    ? kColGold
                       : ctl == g_sub || ctl == g_versions ? kColDim
                                                           : kColText);
      return (LRESULT)g_bgBrush;
    }
    case WM_DRAWITEM: {
      DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
      if (d->CtlID == 1) {
        bool enabled = IsWindowEnabled(d->hwndItem) != 0;
        COLORREF fill = enabled ? kColGreen : RGB(0x2a, 0x2c, 0x30);
        HBRUSH b = CreateSolidBrush(fill);
        FillRect(d->hDC, &d->rcItem, b);
        DeleteObject(b);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, enabled ? RGB(0x08, 0x09, 0x0b) : kColDim);
        HFONT old = (HFONT)SelectObject(d->hDC, g_btnFont);
        wchar_t text[64];
        GetWindowTextW(d->hwndItem, text, 64);
        DrawTextW(d->hDC, text, -1, &d->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(d->hDC, old);
        return TRUE;
      }
      break;
    }
    case WM_COMMAND:
      if (LOWORD(wp) == 1 && HIWORD(wp) == BN_CLICKED) OnPlayClicked();
      return 0;
    case kMsgStatus: {
      std::unique_ptr<std::wstring> text((std::wstring*)lp);
      if (wp == 1) {
        SetWindowTextW(g_versions, text->c_str());
      } else {
        SetWindowTextW(g_status, text->c_str());
      }
      return 0;
    }
    case kMsgProgress: {
      long permille = (long)wp;
      if (permille < 0) {
        LONG_PTR style = GetWindowLongPtrW(g_bar, GWL_STYLE);
        SetWindowLongPtrW(g_bar, GWL_STYLE, style | PBS_MARQUEE);
        SendMessageW(g_bar, PBM_SETMARQUEE, TRUE, 30);
      } else {
        LONG_PTR style = GetWindowLongPtrW(g_bar, GWL_STYLE);
        if (style & PBS_MARQUEE) {
          SendMessageW(g_bar, PBM_SETMARQUEE, FALSE, 0);
          SetWindowLongPtrW(g_bar, GWL_STYLE, style & ~PBS_MARQUEE);
        }
        SendMessageW(g_bar, PBM_SETPOS, (WPARAM)permille, 0);
      }
      return 0;
    }
    case kMsgState: {
      State s = (State)wp;
      g_playEnabled = (s != kWorking);
      EnableWindow(g_play, s != kWorking);
      SetWindowTextW(g_play, s == kPlayAnyway ? L"PLAY ANYWAY" : L"PLAY");
      InvalidateRect(g_play, nullptr, TRUE);
      return 0;
    }
    case kMsgAutoLaunch:
      OnPlayClicked();
      return 0;
    case WM_CLOSE:
      g_cancel = true;
      DestroyWindow(wnd);
      return 0;
    case WM_DESTROY:
      if (g_worker) {
        WaitForSingleObject(g_worker, 5000);
        CloseHandle(g_worker);
        g_worker = nullptr;
      }
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(wnd, msg, wp, lp);
}

}  // namespace

// Plain WinMain (not wWinMain): the launcher takes no command-line
// arguments, and the ANSI entry point links identically under MSVC and
// MinGW with no unicode-entry shim.
int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
  wchar_t selfPath[MAX_PATH];
  GetModuleFileNameW(inst, selfPath, MAX_PATH);
  g_gameDir = DirOf(selfPath);

  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_PROGRESS_CLASS;
  InitCommonControlsEx(&icc);

  g_bgBrush = CreateSolidBrush(kColBg);
  g_titleFont = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  g_btnFont = CreateFontW(19, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  g_bodyFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = g_bgBrush;
  wc.lpszClassName = L"MEAMULauncherWnd";
  RegisterClassW(&wc);

  RECT r{0, 0, 460, 276};
  AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
  int w = r.right - r.left, h = r.bottom - r.top;
  int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
  int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
  g_wnd = CreateWindowW(wc.lpszClassName, L"MEAMU Launcher",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                        x, y, w, h, nullptr, nullptr, inst, nullptr);
  if (!g_wnd) return 1;

  // Title + subtitle statics; colours come from the WM_CTLCOLORSTATIC
  // handler, which recognises these two handles.
  g_title = CreateWindowW(L"STATIC", L"MEAMU", WS_CHILD | WS_VISIBLE | SS_LEFT,
                          24, 18, 412, 40, g_wnd, nullptr, inst, nullptr);
  SendMessageW(g_title, WM_SETFONT, (WPARAM)g_titleFont, TRUE);
  g_sub = CreateWindowW(L"STATIC", L"Senju vs Uchiha · MU Online 0.97k",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        24, 62, 412, 18, g_wnd, nullptr, inst, nullptr);
  SendMessageW(g_sub, WM_SETFONT, (WPARAM)g_bodyFont, TRUE);

  ShowWindow(g_wnd, show);
  UpdateWindow(g_wnd);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}
