# vivlauncher

A Windows desktop application for launching Xilinx Vivado with automatic version detection and selection.

## Features

- Automatically detects installed Vivado versions from multiple locations:
  - `C:\Xilinx` (legacy structure)
  - `C:\Xilinx` (newer structure)
  - `C:\AMDDesignTools`
  - Windows Registry
  - Custom user-added paths
- Modern dark UI: sidebar navigation (Projects / Diagnosis / Help), two-line lists and a themed accent action button
- Version selection dialog when multiple installations are found
- Per-project install binding and a global default installation (stored by install path, so duplicate version strings cannot launch the wrong install)
- `.xpr` file association from the Diagnosis page
- CLI options for listing installations and adding custom paths
- Remembers user-added custom paths between sessions

## Usage

### GUI Mode

Double-click `vivlauncher.exe` or pass a project file as argument:

```
vivlauncher.exe project.xpr
```

If no project file is provided, a file open dialog will appear to select a `.xpr` file.

When multiple Vivado versions are detected, a selection dialog lets you choose which version to launch.

### CLI Options

```
vivlauncher.exe -l, --list        List all detected Vivado versions
vivlauncher.exe -a, --add <path>  Add custom installation path
vivlauncher.exe -h, --help        Show help message
```

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