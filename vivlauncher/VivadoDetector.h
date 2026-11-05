#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct VivadoInstall
{
    std::wstring version;
    std::wstring path;
    std::wstring exePath;
};

struct RecentProject
{
    std::wstring path;
    std::wstring version;
};

struct LaunchSettings
{
    bool noLog = false;
    bool noJournal = false;
    std::wstring extraArgs;
};

struct CliOptions
{
    bool listVersions = false;
    bool addPath = false;
    bool showHelp = false;
    std::wstring xprFile;
    std::wstring customPath;
};

CliOptions ParseCliArgs(int argc, wchar_t* argv[]);
std::vector<VivadoInstall> DetectVivadoInstallations();
std::vector<VivadoInstall> AddCustomPath(const wchar_t* path, std::wstring& error);
std::wstring GetAbsolutePath(const std::wstring& path);
std::wstring LoadDefaultInstallPath();
bool SaveDefaultInstallPath(const std::wstring& installPath);
std::vector<RecentProject> LoadRecentProjects();
void RememberRecentProject(const std::wstring& path, const std::wstring& version);
bool ClearRecentProjects();
std::wstring LoadProjectInstallPath(const std::wstring& path);
bool SaveProjectInstallPath(const std::wstring& path, const std::wstring& installPath);
bool RegisterXprFileAssociation();
LaunchSettings LoadLaunchSettings();
bool SaveLaunchSettings(const LaunchSettings& settings);
std::wstring GetSettingsFilePath();
std::wstring GetLogFilePath();
bool LaunchVivado(const wchar_t* vivadoExePath, const wchar_t* xprFilePath,
                  const LaunchSettings& settings = {});
