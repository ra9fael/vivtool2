// Headless unit tests for the pure launcher logic (VivadoLogic.h).
// Build via tests/vivlauncher_tests.vcxproj; the binary exits non-zero on failure.

#include "../vivlauncher/VivadoLogic.h"

#include <cstdio>

static int failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            ++failures; \
            wprintf(L"FAIL line %d: %hs\n", __LINE__, #cond); \
        } \
    } while (0)

static VivadoInstall MakeInstall(const wchar_t* version, const wchar_t* path)
{
    VivadoInstall inst;
    inst.version = version;
    inst.path = path;
    inst.exePath = std::wstring(path) + L"\\bin\\vivado.bat";
    return inst;
}

int wmain()
{
    using namespace vivlogic;

    // Version parsing
    CHECK(ParseVersionTuple(L"2024.2") == std::make_tuple(2024, 2));
    CHECK(ParseVersionTuple(L"2023.1") == std::make_tuple(2023, 1));
    CHECK(ParseVersionTuple(L"Unknown") == std::make_tuple(0, 0));
    CHECK(ParseVersionTuple(L"garbage") == std::make_tuple(0, 0));
    CHECK(ParseVersionTuple(L"2025") == std::make_tuple(2025, 0));

    // Path key normalization
    CHECK(NormalizePathKey(L"C:\\Xilinx\\Vivado\\") == NormalizePathKey(L"c:\\xilinx\\vivado"));
    CHECK(NormalizePathKey(L"D:\\Tools\\Vivado\\") == L"d:\\tools\\vivado");
    CHECK(NormalizePathKey(L"").empty());

    // SamePath
    CHECK(SamePath(L"C:\\A", L"c:\\a"));
    CHECK(!SamePath(L"C:\\A", L""));
    CHECK(!SamePath(L"", L"C:\\A"));

    // Ordering: newer first
    VivadoInstall newInst = MakeInstall(L"2024.2", L"C:\\Xilinx\\Vivado\\2024.2");
    VivadoInstall oldInst = MakeInstall(L"2023.1", L"D:\\Xilinx\\Vivado\\2023.1");
    CHECK(CompareInstallVersions(newInst, oldInst));
    CHECK(!CompareInstallVersions(oldInst, newInst));
    CHECK(!CompareInstallVersions(newInst, newInst));

    // Ordering: equal versions sort by path (deterministic secondary key)
    VivadoInstall cDrive = MakeInstall(L"2024.2", L"C:\\A");
    VivadoInstall dDrive = MakeInstall(L"2024.2", L"D:\\A2");
    CHECK(CompareInstallVersions(cDrive, dDrive));
    CHECK(!CompareInstallVersions(dDrive, cDrive));

    // Unknown versions sort last
    VivadoInstall unknown = MakeInstall(L"Unknown", L"E:\\Custom");
    CHECK(CompareInstallVersions(newInst, unknown));
    CHECK(!CompareInstallVersions(unknown, newInst));

    if (failures == 0)
    {
        wprintf(L"All tests passed\n");
        return 0;
    }
    wprintf(L"%d test(s) failed\n", failures);
    return 1;
}
