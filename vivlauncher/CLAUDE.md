# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```powershell
cd vivlauncher
.\build.ps1                # Release|x64, MSBuild located via vswhere
.\build.ps1 -Tests         # also runs tests\vivlauncher_tests.exe
```

Manual: `msbuild vivlauncher\vivlauncher.vcxproj /p:Configuration=Release /p:Platform=x64`

Requires a Visual Studio edition with the MSVC v145 toolset and Windows SDK 10.0.

Output: `vivlauncher\x64\<Config>\vivlauncher.exe`. Close a running instance first, or linking fails with LNK1104.

## Architecture

**vivlauncher** is a Windows desktop app (Win32, C++20, x64) that detects installed Vivado versions and launches the selected one with a project file.

### Entry and flow

`vivlauncher.cpp wWinMain`: sets per-monitor-v2 DPI awareness, parses CLI args, then branches:
- `--help` / `--list` / `--add`: print and exit. The GUI-subsystem binary attaches to the parent console only when stdout is not redirected, so file/pipe redirection keeps working.
- GUI mode: opens `IDD_MAIN_DIALOG` (`MainDialogProc`) with the project path from argv (may be empty).

### Detection (`VivadoDetector.cpp`)

`DetectVivadoInstallations()` iterates **all fixed drives** via `GetLogicalDriveStringsW`, scanning three path patterns per drive:
- `X:\Xilinx\Vivado\<version>\` (old structure)
- `X:\Xilinx\<version>\Vivado\` (new structure)
- `X:\AMDDesignTools\<version>\Vivado\` (AMD structure)

Plus registry (`HKLM\SOFTWARE\Xilinx\Vivado` and WOW6432Node) and custom paths in `%APPDATA%\vivlauncher\paths.txt`.

Each installation must contain `bin\vivado.bat`. Results are deduplicated by normalized `exePath` and sorted descending by version.

Settings live in `%APPDATA%\vivlauncher\settings.ini`. Default and per-project bindings store the **install path** (`default_install`, `[project_installs]`), not the version string; old `default_version` / `[project_versions]` keys are migrated on read.

### Pure logic (`VivadoLogic.h`)

Header-only `vivlogic::` functions with no GUI dependency: version parsing/ordering and path normalization. Covered by `tests\vivlauncher_tests.cpp`.

### Launch (`LaunchVivado`)

Prefers `bin\unwrapped\win64.o\vvgl.exe` when launching `.bat` files: resolves the vvgl path from the bat's parent directory and passes the bat path as the first argument. Uses `ShellExecuteExW`. Each launch is appended to `%LOCALAPPDATA%\vivlauncher\vivlauncher.log`.

### UI (`SelectorDialog.cpp`, `theme.h`, `vivlauncher.rc`)

- `IDD_MAIN_DIALOG`: sidebar `IDC_NAV_LIST` switches three pages via `ShowMainPage`; every page control must be listed in one of its control groups, or it shows on all pages.
- `IDD_VERSION_SELECTOR`: list with keyboard (j/k/Enter/Esc/?) and mouse navigation.
- `IDD_ADD_PATH`: manual path entry with browse button.

Dark theme rules:
- All colors come from `theme.h`; brushes from `GetThemeBrush`.
- Buttons and checkboxes are `BS_OWNERDRAW`, subclassed with `SubclassOwnerButton`, which must forward to the saved stock proc (the stock proc turns WM_PAINT into WM_DRAWITEM). Draw functions must fill the whole item rect first, because the stock proc erases with a light brush.
- LISTBOX/EDITTEXT in the rc need `NOT WS_BORDER`; edit frames are painted by `PaintEditBorders` in the dialog's WM_PAINT.
- Single-line edits are padded and vertically centered via `PadEdit` (WM_NCCALCSIZE subclass); call it after the edit's final font is set.
- Read-only edits arrive as WM_CTLCOLORSTATIC; `DarkDialogColor` checks the class name to keep the edit background.

### Coding conventions

- Strings: Unicode (`std::wstring`, `L"..."`), wide Win32 API (`...W`).
- `std::filesystem` calls use the `std::error_code` overloads; never let a scan throw.
- Source files stay ASCII (the build uses CP936, non-ASCII triggers C4819).
- Indentation: 4 spaces, Allman braces.
- Commits follow the Angular convention (`feat(ui): ...`, `fix(cli): ...`, `docs: ...`).
- UI changes need a manual check; CLI can be verified with `--list` redirected to a file.
