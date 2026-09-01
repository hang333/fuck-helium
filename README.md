# fuck-helium

`fuck-helium` makes Helium's **Exit** command a top-level item in the three-dot
app menu instead of placing it under **More tools**.

It is intentionally limited to that one behavior. It does not patch files in
Helium's version directory and it does not disable Helium's updater.

## How it works

Helium builds the app menu in C++ inside `chrome.dll`; this is not an extension
or `resources.pak` setting. `fuck-helium` therefore uses the same Windows DLL
side-loading entry point as Chrome++: a proxy `version.dll` sits next to
`chrome.exe` and forwards the three Version API functions Helium imports.

After `chrome.dll` loads, the proxy locates the menu helper using structural
machine-code checks. It suppresses only the validated More tools Exit call and
replays that same localized label and icon immediately after the validated
Settings call in the parent menu.

No address is hard-coded. The patch is fail-closed: if an update changes the
code generator or menu structure enough that exactly one validated sequence
cannot be found, the DLL leaves the browser untouched and records the reason in
`%LOCALAPPDATA%\fuck-helium\fuck-helium.log`.

## Supported target

- Windows 10 or newer
- x64 Helium
- Tested against Helium `0.16.2.1` / Chromium `152.0.7977.64`

ARM64 is deliberately rejected at configure time until it has its own validated
hook implementation.

## Build

Install Visual Studio 2022 Build Tools with **Desktop development with C++** and
**CMake tools for Windows**, then run:

```powershell
.\build.cmd
```

Artifacts are written to `out\x64\release`.

## Releases

Run the **Release** workflow manually from the GitHub Actions page. When no
date is supplied, it uses the current date in Malaysia time and creates a
`YYYY.MM.DD` tag and GitHub Release, for example `2026.09.01`.

The same date version is embedded in `version.dll` and
`fuck-helium-keeper.exe`. Each release contains an x64 ZIP package and a
SHA-256 checksum file. A date can be released only once; the workflow fails
instead of replacing an existing tag or release. The optional `release_date`
input can be used to publish a specific date.

## Install

Run the packaged `install.ps1`, or from the repository after building:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\install.ps1
```

The script detects the per-user or system Helium installation. A system install
requires elevation. It refuses to overwrite an unrelated `version.dll` unless
`-Force` is explicitly supplied.

Completely exit Helium and start it again after installation.

## Surviving updater upgrades

The primary DLL lives in Helium's stable `Application` directory, next to
`chrome.exe`, rather than in a numbered version directory. Chromium-style
updates replace the numbered directory and launchers; they normally leave
unknown side-loaded files in the stable directory alone.

For defense in depth, the installer also stores a canonical copy under
`%LOCALAPPDATA%\fuck-helium` for per-user installs or the administrator-only
`%ProgramFiles%\fuck-helium` directory for system installs, and registers the
`fuck-helium update guard` logon task. Its small native keeper checks twice a
second. If an installer removes `Application\version.dll`, it waits until the
launcher has been stable for five seconds before restoring the proxy. The keeper
is started immediately at install time, so it is already outside the Helium
install tree while an in-app update is applied.

Use `-NoUpdateGuard` during installation to opt out of the scheduled task.

## Uninstall

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1
```

The uninstaller removes `Application\version.dll` only when its SHA-256 hash
matches the canonical DLL installed by this project.

## Repository layout

The local `chrome_plus/` and `helium-windows/` directories are reference
checkouts and are excluded by `.gitignore`. They are not part of this project or
its GitHub history.
