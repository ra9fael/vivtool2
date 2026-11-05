// vivlauncher.cpp : Defines the entry point for the application.

#include "framework.h"
#include "vivlauncher.h"
#include "VivadoDetector.h"
#include "SelectorDialog.h"
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

#define MAX_LOADSTRING 100

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

static void PrintHelp()
{
    wprintf(L"RUNTIME USAGE:\n");
    wprintf(L"  vivlauncher.exe [project.xpr]     Open project with detected Vivado\n");
    wprintf(L"\nCLI OPTIONS:\n");
    wprintf(L"  -l, --list                       List all detected Vivado versions\n");
    wprintf(L"  -a, --add <path>                  Add custom installation path\n");
    wprintf(L"  -h, --help                       Show this help message\n");
}

static void PrintVersionList(const std::vector<VivadoInstall>& installs)
{
    if (installs.empty())
    {
        wprintf(L"No Vivado installation found.\n");
        return;
    }

    wprintf(L"Detected Vivado installations:\n\n");

    for (size_t i = 0; i < installs.size(); ++i)
    {
        const auto& inst = installs[i];
        wprintf(L"[%zu] %s\n      %s\n\n", i + 1, inst.version.c_str(), inst.exePath.c_str());
    }
}

static std::wstring GetXprPathFromArgs(const CliOptions& opts)
{
    return opts.xprFile;
}

static int OpenProjectWithSelector(const std::wstring& xprPath, const std::vector<VivadoInstall>& installs)
{
    INT_PTR result = DialogBoxParamW(hInst, MAKEINTRESOURCE(IDD_VERSION_SELECTOR),
                                  nullptr, SelectorDialogProc, (LPARAM)&installs);
    if (result <= 0)
    {
        return -1;
    }

    int idx = (int)result - 1;
    if (idx >= 0 && idx < (int)installs.size())
    {
        LaunchVivado(installs[idx].exePath.c_str(), xprPath.c_str());
    }

    return idx;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    INITCOMMONCONTROLSEX commonControls = { sizeof(commonControls), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&commonControls);

    // Crisp rendering on any monitor scale; must be set before any window is created.
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    CliOptions opts = ParseCliArgs(argc, argv);
    LocalFree(argv);

    if (opts.listVersions || opts.addPath || opts.showHelp)
    {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
        {
            FILE* fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
        }
    }

    if (opts.showHelp)
    {
        PrintHelp();
        return 0;
    }

    if (opts.listVersions)
    {
        auto installs = DetectVivadoInstallations();
        PrintVersionList(installs);
        return 0;
    }

    if (opts.addPath)
    {
        if (opts.customPath.empty())
        {
            fwprintf(stderr, L"Error: --add requires a path argument\n");
            return 1;
        }
        AddCustomPath(opts.customPath.c_str());
        wprintf(L"Added: %s\n", opts.customPath.c_str());
        return 0;
    }

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_VIVLAUNCHER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    std::wstring xprPath = GetXprPathFromArgs(opts);
    if (!xprPath.empty())
        xprPath = GetAbsolutePath(xprPath);

    DialogBoxParamW(hInst, MAKEINTRESOURCE(IDD_MAIN_DIALOG), nullptr,
                    MainDialogProc, (LPARAM)xprPath.c_str());
    return 0;
}



ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_VIVLAUNCHER));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_VIVLAUNCHER);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    ShowWindow(hWnd, SW_HIDE);
    UpdateWindow(hWnd);

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
    }
    break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}
