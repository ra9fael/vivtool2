# build.ps1 - Build vivlauncher (and run the logic tests) with any VS edition.

param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [switch]$Tests
)

$ErrorActionPreference = "Stop"

# Locate the newest Visual Studio installation via vswhere; works for
# Community / Professional / Enterprise / Build Tools regardless of install path.
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Install Visual Studio or Build Tools for Visual Studio."
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
    -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
if (-not $msbuild) {
    throw "No Visual Studio installation with MSBuild was found."
}

$solution = Join-Path $PSScriptRoot "vivlauncher.slnx"
Write-Host "Building $solution [$Configuration|$Platform] via $msbuild" -ForegroundColor Cyan

& $msbuild $solution "/p:Configuration=$Configuration" "/p:Platform=$Platform" /m /v:m /nologo
$exitCode = $LASTEXITCODE

if ($Tests -and $exitCode -eq 0) {
    $testExe = Join-Path $PSScriptRoot "..\tests\x64\$Configuration\vivlauncher_tests.exe"
    Write-Host "`nRunning logic tests..." -ForegroundColor Cyan
    & $testExe
    $exitCode = $LASTEXITCODE
}

if ($exitCode -eq 0) {
    Write-Host "`nBuild successful!" -ForegroundColor Green
} else {
    Write-Host "`nBuild failed with exit code $exitCode" -ForegroundColor Red
}

exit $exitCode
