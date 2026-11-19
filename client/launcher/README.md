# MEAMU Launcher

Auto-updating starter for the MEAMU desktop client. Run
`MEAMU-Launcher.exe` instead of `main.exe`:

- Reads the local version from the stamp embedded in `main.exe`
  (`vYYYYMMDD-HHMM`).
- Fetches the live version config from `https://meamu.ca`
  (`patchVersion` is the source of truth).
- Out of date → downloads the update pack, verifies the new exe's stamp
  **before** swapping it in (the old exe is kept as
  `main.exe.bak-<old version>`), then waits for the Play click.
- No `main.exe` → downloads and unpacks the full client.
- Already current → launches the game automatically after ~1 s.
- No connection / failed download → nothing is changed; **Play anyway**
  starts the version on disk.

Build: `cmake -S . -B build -A Win32 -T v143 && cmake --build build
--config Release` — or dispatch `.github/workflows/launcher.yml`.
Zip extraction is vendored miniz (see `src/miniz-LICENSE.txt`); downloads
use WinHTTP. The CRT is linked statically: the exe runs on a bare Windows
install with no redistributable.
