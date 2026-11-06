# GEMINI.md - vivtool2

## Project Overview

**vivtool2** is a Windows desktop application (`vivlauncher`) designed to streamline the process of launching Xilinx Vivado. It detects installations from common paths, the Windows Registry and user-added paths, and opens a dark-themed main window (Projects / Diagnosis / Help) for choosing the project and installation.

- **Primary Language:** C++20
- **Framework:** Native Win32 API
- **Target Platform:** Windows (x64)
- **IDE:** Visual Studio with the MSVC v145 toolset

### Architecture
`vivlauncher/vivlauncher.slnx` contains the `vivlauncher` app and the `tests/vivlauncher_tests` console project.
- `vivlauncher.cpp`: Entry point (`wWinMain`), DPI awareness and CLI argument parsing.
- `VivadoDetector.cpp/h`: Scanning drives, registry and custom paths; settings, bindings and launching.
- `VivadoLogic.h`: Header-only pure logic (version ordering, path normalization), unit-tested.
- `SelectorDialog.cpp/h`: Main window, version selector and add-path dialogs, all owner-drawn.
- `theme.h`: Dark color palette used by all custom drawing.
- `vivlauncher.rc`: Resource definitions (dialogs, icons, strings).

## Building and Running

### Prerequisites
- Windows 10/11
- Visual Studio with **MSVC v145** toolset (located automatically via vswhere)
- Windows SDK 10.0

### Build Commands

**Visual Studio:**
1. Open `vivlauncher/vivlauncher.slnx`.
2. Select `Debug` or `Release` configuration and `x64` platform.
3. Build Solution (F7).

**PowerShell Script (Recommended):**
```powershell
cd vivlauncher
.\build.ps1              # Release|x64
.\build.ps1 -Tests       # also runs the unit tests
```

**Manual Command Line (MSBuild):**
```batch
msbuild vivlauncher\vivlauncher.vcxproj /p:Configuration=Release /p:Platform=x64
```

### Running
The application can be run in GUI or CLI mode.
- **GUI:** `vivlauncher.exe [project.xpr]`
- **CLI:**
  - `vivlauncher.exe -l` (List detected versions)
  - `vivlauncher.exe -a <path>` (Add custom installation path)

Output binaries are located in `vivlauncher/x64/<Configuration>/`.

## Development Conventions

### Coding Style
- **Indentation:** 4 spaces.
- **Braces:** Allman style (braces on their own line).
- **Naming:** Follows standard Win32/Visual Studio template conventions (`InitInstance`, `WndProc`).
- **Strings:** Use Unicode (`WCHAR`, `std::wstring`, `L"string"` literals) throughout.
- **Filesystem:** Use the `std::error_code` overloads of `std::filesystem`.
- **Encoding:** Keep source files ASCII-only (non-ASCII triggers C4819 under CP936).
- **Commits:** Angular convention (`feat(ui): ...`, `fix(cli): ...`, `docs: ...`).

### UI & Resources
- Use `Resource.h` for ID management (`IDD_*`, `IDC_*`, `IDI_*`).
- Dialogs are defined in `vivlauncher.rc`. LISTBOX and EDITTEXT controls need `NOT WS_BORDER`; frames are drawn by the dialog.
- Owner-drawn buttons must forward messages to the stock button proc and fill their full rect before drawing.
- Every main-window control must belong to one page group in `ShowMainPage`.

### Testing
- `tests/vivlauncher_tests.cpp` covers the pure logic in `VivadoLogic.h`; run with `.\build.ps1 -Tests`.
- **Manual Verification:** check the UI pages visually, and verify detection with `vivlauncher.exe --list` (redirect output to a file when running from a script).

## Key Files
- `vivlauncher/vivlauncher.cpp`: Application lifecycle and CLI integration.
- `vivlauncher/VivadoDetector.cpp`: Core logic for finding Xilinx installations.
- `vivlauncher/SelectorDialog.cpp`: Main window and dialog UI.
- `vivlauncher/theme.h`: Dark theme palette.
- `vivlauncher/vivlauncher.vcxproj`: Project configuration and build settings.
