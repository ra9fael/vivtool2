# vivlauncher

A Windows desktop application for launching Xilinx Vivado with automatic version detection and selection.

![vivlauncher main window](../docs/screenshot.png)

## Features

- Automatically detects installed Vivado versions from multiple locations:
  - `C:\Xilinx` (legacy structure)
  - `C:\Xilinx` (newer structure)
  - `C:\AMDDesignTools`
  - Windows Registry
  - Custom user-added paths
- Dark UI with sidebar navigation:
  - **Projects**: project path, recent projects, detected installations, launch options (`-nolog`, `-nojournal`, extra arguments)
  - **Diagnosis**: launch log and settings locations, log/config folder shortcuts, clear recent projects, `.xpr` file association
  - **Help**: built-in usage guide and command-line reference
- Per-project install binding and a global default installation (stored by install path, so duplicate version strings cannot launch the wrong install)
- Dark caption bar and rounded window corners on Windows 11, per-monitor DPI scaling
- CLI options for listing installations and adding custom paths
- Remembers user-added custom paths between sessions

## Usage

### GUI Mode

Double-click `vivlauncher.exe` or pass a project file as argument:

```
vivlauncher.exe project.xpr
```

The main window opens with the project path filled in (or empty, if no argument was given). Pick a project with **Browse** or from **Recent projects**, select an installation, then click **Open project**. **Bind project** remembers the selected installation for that project; **Set default** sets the fallback for all other projects.

### CLI Options

```
vivlauncher.exe -l, --list        List all detected Vivado versions
vivlauncher.exe -a, --add <path>  Add custom installation path
vivlauncher.exe -h, --help        Show help message
```

CLI output goes to the parent console, or to a file or pipe when redirected (for example `Start-Process vivlauncher.exe --list -RedirectStandardOutput out.txt -Wait`).

## Building

### Prerequisites

- Windows
- Visual Studio 2026 (or any edition providing the MSVC v145 toolset and MSBuild; located automatically via vswhere)
- Windows SDK 10.0

### Build Commands

**Build script (recommended):**
```powershell
cd vivlauncher
.\build.ps1              # Release|x64 via vswhere-detected MSBuild
.\build.ps1 -Tests       # also runs tests\vivlauncher_tests.exe
```

**Visual Studio:**
Open `vivlauncher/vivlauncher.slnx` (includes the app and the test project) and build `Debug|x64` or `Release|x64`.

**Command Line:**
```batch
msbuild vivlauncher\vivlauncher.vcxproj /p:Configuration=Debug /p:Platform=x64
msbuild tests\vivlauncher_tests.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Output: `vivlauncher\x64\Debug\vivlauncher.exe` or `vivlauncher\x64\Release\vivlauncher.exe`

### Tests

Headless unit tests for the pure launcher logic (version parsing, install ordering, path normalization) live in `tests/vivlauncher_tests.cpp`. The test binary exits non-zero on failure, so it can run in CI.

## Technical Details

- Language: C++20
- Framework: Win32 (native Windows API)
- UI: Native Win32 dialogs with a custom dark theme (`theme.h`, owner-drawn lists/buttons), dark caption and rounded corners via DWM
- Project structure follows standard Visual Studio Win32 template