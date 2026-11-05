#include "VivadoDetector.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <shlobj.h>
#include <shellapi.h>
#include <cstdint>

namespace fs = std::filesystem;

// Non-throwing filesystem helpers: a bad drive, permission error or a removed
// directory must never take the launcher down mid-scan.
static bool PathExists(const fs::path& path)
{
    std::error_code ec;
    return fs::exists(path, ec);
}

static void EnsureDirectory(const fs::path& path)
{
    std::error_code ec;
    fs::create_directories(path, ec);
}

static std::vector<fs::path> ListSubdirectories(const fs::path& base)
{
    std::vector<fs::path> dirs;
    std::error_code ec;
    if (!fs::is_directory(base, ec))
        return dirs;
    fs::directory_iterator it(base, ec), end;
    for (; it != end && !ec; it.increment(ec))
    {
        std::error_code entryEc;
        if (it->is_directory(entryEc) && !entryEc)
            dirs.push_back(it->path());
    }
    return dirs;
}

static std::wstring GetConfigPath()
{
    wchar_t appData[MAX_PATH];
    if (SHGetSpecialFolderPathW(nullptr, appData, CSIDL_APPDATA, TRUE))
    {
        return std::wstring(appData) + L"\\vivlauncher\\paths.txt";
    }
    return {};
}

std::wstring GetSettingsFilePath()
{
    auto configPath = GetConfigPath();
    if (configPath.empty())
        return {};
    return (fs::path(configPath).parent_path() / L"settings.ini").native();
}

std::wstring GetLogFilePath()
{
    wchar_t localAppData[MAX_PATH] = {};
    if (!SHGetSpecialFolderPathW(nullptr, localAppData, CSIDL_LOCAL_APPDATA, TRUE))
        return {};
    return (fs::path(localAppData) / L"vivlauncher" / L"vivlauncher.log").native();
}

static fs::path NormalizeVivadoPath(std::wstring path)
{
    while (!path.empty() && (path.back() == L' ' || path.back() == L'\t' || path.back() == L'"'))
        path.pop_back();
    while (!path.empty() && (path.front() == L' ' || path.front() == L'\t' || path.front() == L'"'))
        path.erase(path.begin());

    fs::path result(path);
    if (_wcsicmp(result.filename().c_str(), L"bin") == 0)
        result = result.parent_path();
    return result;
}

static void AppendLaunchLog(const std::wstring& message)
{
    auto logPath = GetLogFilePath();
    if (logPath.empty())
        return;
    EnsureDirectory(fs::path(logPath).parent_path());
    std::wofstream log(logPath, std::ios::app);
    if (log.is_open())
        log << message << L"\n";
}

static std::wstring ProjectSettingsKey(const std::wstring& path)
{
    // INI keys cannot safely contain a Windows path, so use a stable FNV-1a key.
    uint64_t hash = 14695981039346656037ull;
    for (wchar_t c : path)
    {
        wchar_t normalized = (wchar_t)towlower(c);
        hash ^= (uint64_t)normalized;
        hash *= 1099511628211ull;
    }

    wchar_t key[32];
    swprintf_s(key, L"project_%016llX", (unsigned long long)hash);
    return key;
}

static std::vector<std::wstring> LoadCustomPaths()
{
    std::vector<std::wstring> paths;
    auto configPath = GetConfigPath();
    std::wstring legacyConfigPath;
    if (!configPath.empty())
        legacyConfigPath = (fs::path(configPath).parent_path() / L"paths.json").native();

    std::wstring inputPath = PathExists(configPath) ? configPath : legacyConfigPath;
    if (PathExists(inputPath))
    {
        std::wifstream file(inputPath);
        if (file.is_open())
        {
            std::wstring line;
            while (std::getline(file, line))
            {
                line.erase(std::remove_if(line.begin(), line.end(),
                    [](wchar_t c) { return c == L'\r' || c == L'\n' || c == L' ' || c == L'\t'; }), line.end());

                if (!line.empty() && line[0] != L'#')
                {
                    fs::path rootPath = NormalizeVivadoPath(line);
                    fs::path vivadoBat = rootPath / L"bin\\vivado.bat";
                    if (PathExists(vivadoBat))
                    {
                        paths.push_back(rootPath.native());
                    }
                }
            }
        }
    }

    return paths;
}

static fs::path FindVivadoExe(const fs::path& basePath)
{
    fs::path batPath = basePath / L"bin\\vivado.bat";
    if (PathExists(batPath))
    {
        return batPath;
    }
    return {};
}

static VivadoInstall MakeInstall(const std::wstring& version, const fs::path& installPath,
                                 const fs::path& exePath)
{
    VivadoInstall inst;
    inst.version = version;
    inst.path = installPath.native();
    inst.exePath = exePath.native();
    return inst;
}

static std::vector<VivadoInstall> ScanOldStructure(const fs::path& basePath)
{
    std::vector<VivadoInstall> installs;

    // Legacy layout: <drive>\Xilinx\Vivado\<version>\bin\vivado.bat
    for (const auto& entry : ListSubdirectories(basePath / L"Vivado"))
    {
        auto exePath = FindVivadoExe(entry);
        if (exePath.empty())
            continue;
        installs.push_back(MakeInstall(entry.filename().native(), entry, exePath));
    }

    return installs;
}

static std::vector<VivadoInstall> ScanNewStructure(const fs::path& basePath)
{
    std::vector<VivadoInstall> installs;

    // Newer layout: <drive>\Xilinx\<version>\Vivado\bin\vivado.bat
    for (const auto& entry : ListSubdirectories(basePath))
    {
        fs::path vivadoSubdir = entry / L"Vivado";
        auto exePath = FindVivadoExe(vivadoSubdir);
        if (exePath.empty())
            continue;
        installs.push_back(MakeInstall(entry.filename().native(), vivadoSubdir, exePath));
    }

    return installs;
}

static std::vector<VivadoInstall> ScanAMDStructure(const fs::path& basePath)
{
    std::vector<VivadoInstall> installs;

    // AMD's current installer uses <drive>\AMDDesignTools\<version>\Vivado.
    for (const auto& entry : ListSubdirectories(basePath))
    {
        fs::path installPath = entry / L"Vivado";
        auto exePath = FindVivadoExe(installPath);
        if (exePath.empty())
            continue;
        installs.push_back(MakeInstall(entry.filename().native(), installPath, exePath));
    }

    return installs;
}

static std::vector<VivadoInstall> ScanRegistry(HKEY root, PCWSTR subkey)
{
    std::vector<VivadoInstall> installs;
    HKEY hKey = nullptr;

    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
        return installs;
    }

    wchar_t buffer[512];
    DWORD cbSize = sizeof(buffer);
    DWORD type = REG_SZ;

    if (RegQueryValueExW(hKey, L"InstallPath", nullptr, &type, (LPBYTE)buffer, &cbSize) == ERROR_SUCCESS)
    {
        auto exePath = FindVivadoExe(buffer);

        if (!exePath.empty())
        {
            VivadoInstall inst;
            inst.version = L"Unknown";
            inst.path = buffer;
            inst.exePath = exePath.native();
            installs.push_back(inst);
        }
    }

    RegCloseKey(hKey);
    return installs;
}

static void AddFromPath(const fs::path& basePath, std::vector<VivadoInstall>& installs)
{
    auto exePath = FindVivadoExe(basePath);

    if (!exePath.empty())
    {
        VivadoInstall inst;
        inst.version = basePath.filename().native();
        inst.path = basePath.native();
        inst.exePath = exePath.native();
        installs.push_back(inst);
    }
}

static bool CompareVersions(const VivadoInstall& a, const VivadoInstall& b)
{
    auto parseVersion = [](const std::wstring& v) -> std::tuple<int, int>
    {
        int major = 0, minor = 0;
        if (v == L"Unknown") return {0, 0};
        swscanf_s(v.c_str(), L"%d.%d", &major, &minor);
        return {major, minor};
    };

    auto [amaj, amin] = parseVersion(a.version);
    auto [bmaj, bmin] = parseVersion(b.version);

    if (amaj != bmaj) return amaj > bmaj;
    if (amin != bmin) return amin > bmin;
    // Stable secondary order so equal versions keep a deterministic position.
    return _wcsicmp(a.path.c_str(), b.path.c_str()) < 0;
}

// Case-insensitive, fully-resolved path identity used for dedup and binding.
static std::wstring PathKey(const std::wstring& path)
{
    std::wstring key = GetAbsolutePath(path);
    while (!key.empty() && (key.back() == L'\\' || key.back() == L'/'))
        key.pop_back();
    std::transform(key.begin(), key.end(), key.begin(), towlower);
    return key;
}

static void Deduplicate(std::vector<VivadoInstall>& installs)
{
    std::sort(installs.begin(), installs.end(), CompareVersions);
    auto it = std::unique(installs.begin(), installs.end(),
        [](const VivadoInstall& a, const VivadoInstall& b) {
            return PathKey(a.exePath) == PathKey(b.exePath);
        });
    installs.erase(it, installs.end());
}

std::vector<VivadoInstall> DetectVivadoInstallations()
{
    std::vector<VivadoInstall> installs;

    // 1. Iterate through all logical drives
    wchar_t drives[512];
    DWORD length = GetLogicalDriveStringsW(ARRAYSIZE(drives) - 1, drives);
    if (length > 0 && length < ARRAYSIZE(drives))
    {
        wchar_t* drive = drives;
        while (*drive)
        {
            // Only scan local fixed drives to avoid network lag or removable media popups
            if (GetDriveTypeW(drive) == DRIVE_FIXED)
            {
                std::wstring drivePath = drive; // e.g., "C:\"
                
                auto oldStruct = ScanOldStructure(drivePath + L"Xilinx");
                installs.insert(installs.end(), oldStruct.begin(), oldStruct.end());

                auto newStruct = ScanNewStructure(drivePath + L"Xilinx");
                installs.insert(installs.end(), newStruct.begin(), newStruct.end());

                auto amdStruct = ScanAMDStructure(drivePath + L"AMDDesignTools");
                installs.insert(installs.end(), amdStruct.begin(), amdStruct.end());
            }
            drive += wcslen(drive) + 1;
        }
    }

    // 2. Scan Registry
    auto registry = ScanRegistry(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Xilinx\\Vivado");
    installs.insert(installs.end(), registry.begin(), registry.end());

    auto registryWow = ScanRegistry(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Xilinx\\Vivado");
    installs.insert(installs.end(), registryWow.begin(), registryWow.end());

    // 3. Scan Custom Paths
    auto custom = LoadCustomPaths();
    for (const auto& path : custom)
    {
        AddFromPath(path, installs);
    }

    // 4. Uniform deduplication and sorting
    Deduplicate(installs);
    return installs;
}

std::vector<VivadoInstall> AddCustomPath(const wchar_t* path, std::wstring& error)
{
    error.clear();
    if (!path || !*path)
    {
        error = L"Path is empty.";
        return {};
    }

    auto configPath = GetConfigPath();
    if (configPath.empty())
    {
        error = L"Cannot resolve the configuration folder.";
        return {};
    }

    fs::path rootPath = NormalizeVivadoPath(path);
    if (!PathExists(rootPath / L"bin\\vivado.bat"))
    {
        error = L"No bin\\vivado.bat found under this path.";
        return {};
    }

    std::wstring newPath = rootPath.native();
    for (const auto& existing : LoadCustomPaths())
    {
        if (_wcsicmp(existing.c_str(), newPath.c_str()) == 0)
        {
            error = L"This path is already registered.";
            return {};
        }
    }

    EnsureDirectory(fs::path(configPath).parent_path());
    std::wofstream file(configPath, std::ios::app);
    if (!file.is_open())
    {
        error = L"Cannot write the paths file.";
        return {};
    }
    file << newPath << L"\n";
    file.flush();
    if (!file.good())
    {
        error = L"Cannot write the paths file.";
        return {};
    }

    return DetectVivadoInstallations();
}

static bool MatchInstallByVersion(const std::wstring& version, std::wstring& installPath)
{
    if (version.empty())
        return false;
    for (const auto& inst : DetectVivadoInstallations())
    {
        if (_wcsicmp(inst.version.c_str(), version.c_str()) == 0)
        {
            installPath = inst.path;
            return true;
        }
    }
    return false;
}

std::wstring LoadDefaultInstallPath()
{
    wchar_t value[MAX_PATH * 2] = {};
    auto settingsPath = GetSettingsFilePath();
    if (settingsPath.empty())
        return {};
    GetPrivateProfileStringW(L"vivlauncher", L"default_install", L"", value,
                             ARRAYSIZE(value), settingsPath.c_str());
    if (value[0])
        return value;
    // Migrate from the legacy default_version key.
    wchar_t old[64] = {};
    GetPrivateProfileStringW(L"vivlauncher", L"default_version", L"", old,
                             ARRAYSIZE(old), settingsPath.c_str());
    std::wstring path;
    if (MatchInstallByVersion(old, path))
        return path;
    return {};
}

bool SaveDefaultInstallPath(const std::wstring& installPath)
{
    auto settingsPath = GetSettingsFilePath();
    if (settingsPath.empty())
        return false;
    EnsureDirectory(fs::path(settingsPath).parent_path());
    return WritePrivateProfileStringW(L"vivlauncher", L"default_install", installPath.c_str(),
                                      settingsPath.c_str()) != FALSE;
}

std::vector<RecentProject> LoadRecentProjects()
{
    std::vector<RecentProject> projects;
    auto settingsPath = GetSettingsFilePath();
    if (settingsPath.empty())
        return projects;

    for (int i = 0; i < 20; ++i)
    {
        wchar_t key[32], value[32768] = {};
        swprintf_s(key, L"recent_%d", i);
        GetPrivateProfileStringW(L"recent_projects", key, L"", value,
                                 ARRAYSIZE(value), settingsPath.c_str());
        if (!value[0])
            continue;

        std::wstring item = value;
        size_t separator = item.find(L'|');
        RecentProject project;
        project.version = separator == std::wstring::npos ? L"" : item.substr(0, separator);
        project.path = separator == std::wstring::npos ? item : item.substr(separator + 1);
        if (!project.path.empty() && PathExists(project.path))
            projects.push_back(std::move(project));
    }
    return projects;
}

void RememberRecentProject(const std::wstring& path, const std::wstring& version)
{
    auto settingsPath = GetSettingsFilePath();
    if (settingsPath.empty())
        return;
    EnsureDirectory(fs::path(settingsPath).parent_path());

    std::vector<RecentProject> projects{{path, version}};
    for (const auto& project : LoadRecentProjects())
    {
        if (_wcsicmp(project.path.c_str(), path.c_str()) != 0 && projects.size() < 20)
            projects.push_back(project);
    }

    for (int i = 0; i < 20; ++i)
    {
        wchar_t key[32];
        swprintf_s(key, L"recent_%d", i);
        if (i < (int)projects.size())
        {
            std::wstring value = projects[i].version + L"|" + projects[i].path;
            WritePrivateProfileStringW(L"recent_projects", key, value.c_str(), settingsPath.c_str());
        }
        else
        {
            WritePrivateProfileStringW(L"recent_projects", key, nullptr, settingsPath.c_str());
        }
    }
}

bool ClearRecentProjects()
{
    auto settingsPath = GetSettingsFilePath();
    if (settingsPath.empty())
        return false;
    return WritePrivateProfileStringW(L"recent_projects", nullptr, nullptr,
                                      settingsPath.c_str()) != FALSE;
}

std::wstring LoadProjectInstallPath(const std::wstring& path)
{
    wchar_t value[MAX_PATH * 2] = {};
    auto settingsPath = GetSettingsFilePath();
    if (settingsPath.empty())
        return {};
    std::wstring absolutePath = GetAbsolutePath(path);
    std::wstring key = ProjectSettingsKey(absolutePath);
    GetPrivateProfileStringW(L"project_installs", key.c_str(), L"",
                             value, ARRAYSIZE(value), settingsPath.c_str());
    if (value[0])
        return value;
    // Migrate from the legacy version-only binding.
    wchar_t old[64] = {};
    GetPrivateProfileStringW(L"project_versions", key.c_str(), L"",
                             old, ARRAYSIZE(old), settingsPath.c_str());
    std::wstring installPath;
    if (MatchInstallByVersion(old, installPath))
        return installPath;
    return {};
}

bool SaveProjectInstallPath(const std::wstring& path, const std::wstring& installPath)
{
    auto settingsPath = GetSettingsFilePath();
    if (settingsPath.empty())
        return false;
    EnsureDirectory(fs::path(settingsPath).parent_path());
    std::wstring absolutePath = GetAbsolutePath(path);
    return WritePrivateProfileStringW(L"project_installs", ProjectSettingsKey(absolutePath).c_str(),
                                      installPath.c_str(), settingsPath.c_str()) != FALSE;
}

bool RegisterXprFileAssociation()
{
    wchar_t modulePath[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath))
        return false;

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.xpr", 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    const wchar_t* progId = L"VivLauncher.Project";
    LRESULT setProgId = RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE*)progId,
                                       (DWORD)((wcslen(progId) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (setProgId != ERROR_SUCCESS)
        return false;

    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Classes\\VivLauncher.Project\\shell\\open\\command",
                        0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    std::wstring command = L"\"" + std::wstring(modulePath) + L"\" \"%1\"";
    LRESULT setCommand = RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE*)command.c_str(),
                                        (DWORD)((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (setCommand != ERROR_SUCCESS)
        return false;

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

LaunchSettings LoadLaunchSettings()
{
    LaunchSettings settings;
    auto settingsPath = GetSettingsFilePath();
    wchar_t value[32768] = {};
    GetPrivateProfileStringW(L"launch", L"no_log", L"1", value, ARRAYSIZE(value), settingsPath.c_str());
    settings.noLog = wcscmp(value, L"1") == 0;
    GetPrivateProfileStringW(L"launch", L"no_journal", L"1", value, ARRAYSIZE(value), settingsPath.c_str());
    settings.noJournal = wcscmp(value, L"1") == 0;
    GetPrivateProfileStringW(L"launch", L"extra_args", L"", value, ARRAYSIZE(value), settingsPath.c_str());
    settings.extraArgs = value;
    return settings;
}

bool SaveLaunchSettings(const LaunchSettings& settings)
{
    auto settingsPath = GetSettingsFilePath();
    if (settingsPath.empty())
        return false;
    EnsureDirectory(fs::path(settingsPath).parent_path());
    bool ok = WritePrivateProfileStringW(L"launch", L"no_log", settings.noLog ? L"1" : L"0",
                                         settingsPath.c_str()) != FALSE;
    ok = WritePrivateProfileStringW(L"launch", L"no_journal", settings.noJournal ? L"1" : L"0",
                                    settingsPath.c_str()) != FALSE && ok;
    ok = WritePrivateProfileStringW(L"launch", L"extra_args", settings.extraArgs.c_str(),
                                    settingsPath.c_str()) != FALSE && ok;
    return ok;
}

static std::wstring QuoteIfNeeded(const wchar_t* path)
{
    if (wcschr(path, L' ') != nullptr)
        return L'"' + std::wstring(path) + L'"';
    return path;
}

std::wstring GetAbsolutePath(const std::wstring& path)
{
    if (path.empty())
        return {};

    DWORD capacity = MAX_PATH;
    for (;;)
    {
        std::wstring result(capacity, L'\0');
        DWORD length = GetFullPathNameW(path.c_str(), capacity, result.data(), nullptr);
        if (length == 0)
            return path;
        if (length < capacity - 1)
        {
            result.resize(length);
            return result;
        }
        capacity = length + 1;
    }
}

bool LaunchVivado(const wchar_t* vivadoExePath, const wchar_t* xprFilePath,
                  const LaunchSettings& settings)
{
    std::wstring exeToLaunch = vivadoExePath;
    std::wstring cmdLine;

    std::wstring ext = fs::path(exeToLaunch).extension().native();
    std::transform(ext.begin(), ext.end(), ext.begin(), towlower);

    bool useVvgl = false;

    if (ext == L".bat" || ext == L".cmd")
    {
        fs::path vvglPath = fs::path(exeToLaunch).parent_path() / L"unwrapped\\win64.o\\vvgl.exe";
        if (fs::exists(vvglPath))
        {
            exeToLaunch = vvglPath.native();
            useVvgl = true;
        }
    }

    // argv[0]: application path itself (standard convention)
    cmdLine = QuoteIfNeeded(exeToLaunch.c_str());

    if (useVvgl)
    {
        cmdLine += L" ";
        cmdLine += QuoteIfNeeded(vivadoExePath);
    }

    if (settings.noLog)
        cmdLine += L" -nolog";
    if (settings.noJournal)
        cmdLine += L" -nojournal";
    if (!settings.extraArgs.empty())
    {
        cmdLine += L" ";
        cmdLine += settings.extraArgs;
    }

    if (xprFilePath && *xprFilePath)
    {
        cmdLine += L" ";
        cmdLine += QuoteIfNeeded(xprFilePath);
    }

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWDEFAULT;

    PROCESS_INFORMATION pi = {};

    std::wstring workingDirectory;
    if (xprFilePath && *xprFilePath)
        workingDirectory = fs::path(xprFilePath).parent_path().native();

    BOOL ok = CreateProcessW(
        exeToLaunch.c_str(),            // lpApplicationName - direct file path, Unicode-safe, no parsing
        cmdLine.data(),                 // lpCommandLine - argv for child process
        nullptr, nullptr,
        FALSE,
        0,
        nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &si, &pi);

    std::wstring logMessage = L"Launch " + std::wstring(ok ? L"succeeded" : L"failed") + L"\n"
        L"  Vivado: " + std::wstring(vivadoExePath) + L"\n"
        L"  Project: " + std::wstring(xprFilePath ? xprFilePath : L"") + L"\n"
        L"  Working directory: " + workingDirectory + L"\n"
        L"  Arguments: " + cmdLine;
    if (!ok)
        logMessage += L"\n  Error: " + std::to_wstring(GetLastError());
    AppendLaunchLog(logMessage);

    if (ok)
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    return false;
}

CliOptions ParseCliArgs(int argc, wchar_t* argv[])
{
    CliOptions opts;

    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i];

        if (arg == L"-l" || arg == L"--list" || arg == L"list")
        {
            opts.listVersions = true;
        }
        else if (arg == L"-a" || arg == L"--add" || arg == L"add")
        {
            opts.addPath = true;
            if (i + 1 < argc)
            {
                opts.customPath = argv[++i];
            }
        }
        else if (arg == L"-h" || arg == L"--help" || arg == L"help")
        {
            opts.showHelp = true;
        }
        else if (arg[0] != L'-')
        {
            std::transform(arg.begin(), arg.end(), arg.begin(), towlower);
            if (arg.size() >= 4 && arg.compare(arg.size() - 4, 4, L".xpr") == 0)
            {
                opts.xprFile = argv[i];
            }
        }
    }

    return opts;
}
