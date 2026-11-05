#pragma once

// Pure, platform-free logic shared by the launcher and the test project.
// Keep anything here free of Win32 GUI dependencies so tests can run headless.

#include "VivadoDetector.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>
#include <tuple>

namespace vivlogic
{
// major.minor pair parsed from a directory name; anything unparseable maps to (0, 0).
inline std::tuple<int, int> ParseVersionTuple(const std::wstring& version)
{
    int major = 0, minor = 0;
    if (version == L"Unknown")
        return {0, 0};
    swscanf_s(version.c_str(), L"%d.%d", &major, &minor);
    return {major, minor};
}

// Newer versions first; equal versions order deterministically by install path.
inline bool CompareInstallVersions(const VivadoInstall& a, const VivadoInstall& b)
{
    auto [aMajor, aMinor] = ParseVersionTuple(a.version);
    auto [bMajor, bMinor] = ParseVersionTuple(b.version);

    if (aMajor != bMajor)
        return aMajor > bMajor;
    if (aMinor != bMinor)
        return aMinor > bMinor;
    return _wcsicmp(a.path.c_str(), b.path.c_str()) < 0;
}

// Case-insensitive path identity with trailing separators removed.
inline std::wstring NormalizePathKey(std::wstring key)
{
    while (!key.empty() && (key.back() == L'\\' || key.back() == L'/'))
        key.pop_back();
    std::transform(key.begin(), key.end(), key.begin(), towlower);
    return key;
}

inline bool SamePath(const std::wstring& a, const std::wstring& b)
{
    if (a.empty() || b.empty())
        return false;
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
}
