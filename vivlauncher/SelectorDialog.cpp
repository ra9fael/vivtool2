#include "SelectorDialog.h"
#include "vivlauncher.h"
#include "theme.h"
#include <string>
#include <vector>
#include <filesystem>
#include <commctrl.h>
#include <dwmapi.h>
#include <map>

#pragma comment(lib, "dwmapi.lib")

namespace fs = std::filesystem;

extern HINSTANCE hInst;
static std::vector<VivadoInstall> g_installs;
static std::wstring g_xprFilePath;
static int g_selectedIndex = 0;
static WNDPROC g_oldListBoxProc = nullptr;

static bool PathExists(const fs::path& path)
{
    std::error_code ec;
    return fs::exists(path, ec);
}

static bool SamePath(const std::wstring& a, const std::wstring& b)
{
    if (a.empty() || b.empty())
        return false;
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

// ---------------------------------------------------------------------------
// Dark theme drawing shared by all dialogs
// ---------------------------------------------------------------------------

static HBRUSH GetThemeBrush(COLORREF color)
{
    static std::map<COLORREF, HBRUSH> cache;
    auto it = cache.find(color);
    if (it == cache.end())
        it = cache.insert({ color, CreateSolidBrush(color) }).first;
    return it->second;
}

static HFONT g_fontItemTitle = nullptr; // list item primary line
static HFONT g_fontItemPath = nullptr;  // list item secondary line
static HFONT g_fontNav = nullptr;       // sidebar entries and buttons
static WNDPROC g_oldOwnerBtnProc = nullptr;

struct ButtonVisual
{
    bool hover = false;
    bool pressed = false;
};
static std::map<HWND, ButtonVisual> g_btnVisual;

static int DpiForHwnd(HWND hWnd)
{
    HDC dc = GetDC(hWnd);
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(hWnd, dc);
    return dpi;
}

static void EnsureThemeFonts(HWND hWnd)
{
    if (g_fontItemTitle)
        return;
    int dpi = DpiForHwnd(hWnd);
    g_fontItemTitle = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontItemPath = CreateFontW(-MulDiv(8, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontNav = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static void DestroyThemeFonts()
{
    if (g_fontItemTitle) { DeleteObject(g_fontItemTitle); g_fontItemTitle = nullptr; }
    if (g_fontItemPath) { DeleteObject(g_fontItemPath); g_fontItemPath = nullptr; }
    if (g_fontNav) { DeleteObject(g_fontNav); g_fontNav = nullptr; }
}

static void ApplyWindowChrome(HWND hDlg)
{
    BOOL dark = TRUE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE: dark caption on Win10 1809+ and Win11.
    DwmSetWindowAttribute(hDlg, 20, &dark, sizeof(dark));
    // DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND (2); ignored on Win10.
    DWORD roundCorners = 2;
    DwmSetWindowAttribute(hDlg, 33, &roundCorners, sizeof(roundCorners));
}

static bool IsSectionLabel(int id)
{
    switch (id)
    {
    case IDC_PROJECT_LABEL: case IDC_RECENT_LABEL: case IDC_INSTALL_LABEL:
    case IDC_ADDITIONAL_LABEL: case IDC_DIAGNOSTIC_LABEL: case IDC_LOG_LABEL:
    case IDC_CONFIG_LABEL: case IDC_HELP_LABEL: case IDC_STATIC:
        return true;
    }
    return false;
}

static LRESULT DarkDialogColor(UINT message, WPARAM wParam, LPARAM lParam)
{
    HDC dc = (HDC)wParam;
    switch (message)
    {
    case WM_CTLCOLORDLG:
        return (LRESULT)GetThemeBrush(theme::WindowBg);
    case WM_CTLCOLORSTATIC:
        SetTextColor(dc, IsSectionLabel(GetDlgCtrlID((HWND)lParam)) ? theme::TextMuted : theme::TextPrimary);
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)GetThemeBrush(theme::WindowBg);
    case WM_CTLCOLOREDIT:
        SetTextColor(dc, theme::TextPrimary);
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)GetThemeBrush(theme::PanelBg);
    case WM_CTLCOLORLISTBOX:
        return (LRESULT)GetThemeBrush(theme::PanelBg);
    }
    return 0;
}

static LRESULT CALLBACK OwnerButtonProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto it = g_btnVisual.find(hWnd);
    if (it == g_btnVisual.end())
        it = g_btnVisual.insert({ hWnd, ButtonVisual() }).first;
    ButtonVisual& vis = it->second;
    switch (message)
    {
    case WM_MOUSEMOVE:
        if (!vis.hover)
        {
            vis.hover = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        break;
    case WM_MOUSELEAVE:
        vis.hover = false;
        InvalidateRect(hWnd, nullptr, TRUE);
        break;
    case WM_LBUTTONDOWN:
        vis.pressed = true;
        InvalidateRect(hWnd, nullptr, TRUE);
        break;
    case WM_LBUTTONUP:
        vis.pressed = false;
        InvalidateRect(hWnd, nullptr, TRUE);
        break;
    case WM_DESTROY:
        g_btnVisual.erase(hWnd);
        break;
    }
    return CallWindowProcW(g_oldOwnerBtnProc, hWnd, message, wParam, lParam);
}

static void SubclassOwnerButton(HWND hDlg, int id)
{
    HWND h = GetDlgItem(hDlg, id);
    if (h)
        SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)OwnerButtonProc);
}

static void DrawOwnerButton(const DRAWITEMSTRUCT* dis)
{
    bool primary = dis->CtlID == IDC_OPEN_PROJECT || dis->CtlID == IDOK;
    const ButtonVisual& vis = g_btnVisual[dis->hwndItem];

    COLORREF bg = primary ? theme::Accent : theme::BtnBg;
    COLORREF fg = primary ? theme::AccentText : theme::TextPrimary;
    COLORREF border = primary ? theme::Accent : theme::BtnBorder;
    if (dis->itemState & ODS_DISABLED)
    {
        bg = theme::PanelBg;
        fg = theme::TextMuted;
        border = theme::PanelBorder;
    }
    else if (vis.pressed)
    {
        bg = primary ? theme::AccentDim : theme::PressedBg;
    }
    else if (vis.hover || (dis->itemState & ODS_FOCUS))
    {
        if (!primary)
            bg = theme::BtnHover;
    }

    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    int dpi = DpiForHwnd(dis->hwndItem);
    int radius = MulDiv(12, dpi, 96);

    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, GetThemeBrush(bg));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    wchar_t text[128] = {};
    GetWindowTextW(dis->hwndItem, text, ARRAYSIZE(text));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    if (g_fontNav)
        SelectObject(dc, g_fontNav);
    DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void DrawOwnerCheckbox(const DRAWITEMSTRUCT* dis, bool checked)
{
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    int dpi = DpiForHwnd(dis->hwndItem);
    int box = MulDiv(14, dpi, 96);
    int y = rc.top + ((rc.bottom - rc.top) - box) / 2;
    RECT boxRc = { rc.left, y, rc.left + box, y + box };

    HPEN pen = CreatePen(PS_SOLID, 1, checked ? theme::Accent : theme::BtnBorder);
    HGDIOBJ oldBrush = SelectObject(dc, GetThemeBrush(checked ? theme::Accent : theme::WindowBg));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    int radius = MulDiv(4, dpi, 96);
    RoundRect(dc, boxRc.left, boxRc.top, boxRc.right, boxRc.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    if (checked)
    {
        HPEN checkPen = CreatePen(PS_SOLID, MulDiv(2, dpi, 96), theme::AccentText);
        HGDIOBJ old = SelectObject(dc, checkPen);
        int l = boxRc.left, t = boxRc.top;
        POINT mid = { l + MulDiv(6, dpi, 96), t + MulDiv(11, dpi, 96) };
        POINT end = { l + MulDiv(11, dpi, 96), t + MulDiv(4, dpi, 96) };
        MoveToEx(dc, l + MulDiv(3, dpi, 96), t + MulDiv(8, dpi, 96), nullptr);
        PolylineTo(dc, &mid, 1);
        PolylineTo(dc, &end, 1);
        SelectObject(dc, old);
        DeleteObject(checkPen);
    }

    wchar_t text[128] = {};
    GetWindowTextW(dis->hwndItem, text, ARRAYSIZE(text));
    RECT trc = { boxRc.right + MulDiv(8, dpi, 96), rc.top, rc.right, rc.bottom };
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, checked ? theme::TextPrimary : theme::TextMuted);
    if (g_fontNav)
        SelectObject(dc, g_fontNav);
    DrawTextW(dc, text, -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void DrawNavItem(const DRAWITEMSTRUCT* dis)
{
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    bool selected = (dis->itemState & ODS_SELECTED) != 0;
    int dpi = DpiForHwnd(dis->hwndItem);

    FillRect(dc, &rc, GetThemeBrush(selected ? theme::NavActiveBg : theme::WindowBg));
    if (selected)
    {
        int bar = MulDiv(3, dpi, 96);
        RECT barRc = { rc.left, rc.top + MulDiv(8, dpi, 96), rc.left + bar, rc.bottom - MulDiv(8, dpi, 96) };
        FillRect(dc, &barRc, GetThemeBrush(theme::Accent));
    }

    wchar_t text[64] = {};
    SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);
    int dot = MulDiv(6, dpi, 96);
    int cx = rc.left + MulDiv(16, dpi, 96);
    int cy = (rc.top + rc.bottom) / 2;
    HGDIOBJ old = SelectObject(dc, GetThemeBrush(selected ? theme::Accent : theme::TextMuted));
    Ellipse(dc, cx - dot / 2, cy - dot / 2, cx + dot / 2, cy + dot / 2);
    SelectObject(dc, old);

    RECT trc = { cx + MulDiv(10, dpi, 96), rc.top, rc.right, rc.bottom };
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, selected ? theme::Accent : theme::TextMuted);
    if (g_fontNav)
        SelectObject(dc, g_fontNav);
    DrawTextW(dc, text, -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void DrawTwoLineItem(const DRAWITEMSTRUCT* dis, const std::wstring& title,
                            const std::wstring& subtitle, bool showBadge)
{
    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;
    bool selected = (dis->itemState & ODS_SELECTED) != 0;
    int dpi = DpiForHwnd(dis->hwndItem);

    FillRect(dc, &rc, GetThemeBrush(selected ? theme::SelectedBg : theme::PanelBg));
    if (selected)
    {
        int bar = MulDiv(3, dpi, 96);
        RECT barRc = { rc.left, rc.top, rc.left + bar, rc.bottom };
        FillRect(dc, &barRc, GetThemeBrush(theme::Accent));
    }

    int pad = MulDiv(14, dpi, 96);
    int lineH = MulDiv(20, dpi, 96);
    int top = rc.top + MulDiv(4, dpi, 96);
    SetBkMode(dc, TRANSPARENT);

    if (g_fontItemTitle)
        SelectObject(dc, g_fontItemTitle);
    SetTextColor(dc, theme::TextPrimary);
    RECT trc = { rc.left + pad, top, rc.right - pad, top + lineH };
    DrawTextW(dc, title.c_str(), -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (showBadge)
    {
        if (g_fontItemPath)
            SelectObject(dc, g_fontItemPath);
        SetTextColor(dc, theme::Accent);
        RECT brc = { rc.right - MulDiv(90, dpi, 96), top, rc.right - MulDiv(8, dpi, 96), top + lineH };
        DrawTextW(dc, L"DEFAULT", -1, &brc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    if (g_fontItemPath)
        SelectObject(dc, g_fontItemPath);
    SetTextColor(dc, theme::TextMuted);
    RECT src = { rc.left + pad, top + lineH, rc.right - MulDiv(8, dpi, 96), rc.bottom - MulDiv(3, dpi, 96) };
    DrawTextW(dc, subtitle.c_str(), -1, &src, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
}

static BOOL HandleMeasureItem(HWND hDlg, LPARAM lParam)
{
    auto mis = (MEASUREITEMSTRUCT*)lParam;
    if (mis->CtlID == IDC_NAV_LIST)
        mis->itemHeight = MulDiv(34, DpiForHwnd(hDlg), 96);
    else
        mis->itemHeight = MulDiv(46, DpiForHwnd(hDlg), 96);
    return TRUE;
}

static void ShowHelp(HWND hWnd)
{
    MessageBoxW(hWnd,
        L"Keyboard & Mouse Shortcuts:\n\n"
        L"  j, Down, Wheel Down : Next version\n"
        L"  k, Up, Wheel Up     : Previous version\n"
        L"  Enter, Space, L-DblClick : Launch selected\n"
        L"  Middle Click, q, Esc : Cancel and Exit\n"
        L"  ?, h : Show this help\n",
        L"Vivado Launcher Help",
        MB_OK | MB_ICONINFORMATION);
}
static LRESULT CALLBACK ListBoxSubclassProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;

    case WM_MOUSEWHEEL:
    {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta > 0) PostMessageW(hWnd, WM_KEYDOWN, VK_UP, 0);
        else if (delta < 0) PostMessageW(hWnd, WM_KEYDOWN, VK_DOWN, 0);
        return 0;
    }

    case WM_MBUTTONDOWN:
    {
        EndDialog(GetParent(hWnd), 0);
        return 0;
    }

    case WM_CHAR:
    {
        wchar_t ch = (wchar_t)wParam;
        switch (ch)
        {
        case L'j': PostMessageW(hWnd, WM_KEYDOWN, VK_DOWN, 0); return 0;
        case L'k': PostMessageW(hWnd, WM_KEYDOWN, VK_UP, 0); return 0;
        case L'?': ShowHelp(GetParent(hWnd)); return 0;
        case L'h': ShowHelp(GetParent(hWnd)); return 0;
        case L'q': EndDialog(GetParent(hWnd), 0); return 0;
        }
        break;
    }
    case WM_KEYDOWN:
    {
        switch (wParam)
        {
        case VK_RETURN:
        case VK_SPACE:
        {
            int selIdx = (int)SendMessageW(hWnd, LB_GETCURSEL, 0, 0);
            if (selIdx >= 0)
            {
                int origIdx = (int)SendMessageW(hWnd, LB_GETITEMDATA, selIdx, 0);
                EndDialog(GetParent(hWnd), origIdx + 1);
            }
            return 0;
        }
        case VK_ESCAPE:
            EndDialog(GetParent(hWnd), 0);
            return 0;
        }
        break;
    }
    }
    return CallWindowProcW(g_oldListBoxProc, hWnd, message, wParam, lParam);
}

static void PopulateList(HWND hList)
{
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);

    for (size_t i = 0; i < g_installs.size(); ++i)
    {
        const auto& inst = g_installs[i];
        std::wstring display = inst.version + L"  (" + inst.path + L")";
        int idx = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)display.c_str());
        SendMessageW(hList, LB_SETITEMDATA, idx, (LPARAM)i);
    }
}

INT_PTR CALLBACK SelectorDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        POINT pt;
        GetCursorPos(&pt);

        RECT rc;
        GetWindowRect(hDlg, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        int x = pt.x - width / 2;
        int y = pt.y - height / 2;

        HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMonitor, &mi))
        {
            x = max(mi.rcWork.left, min(x, mi.rcWork.right - width));
            y = max(mi.rcWork.top, min(y, mi.rcWork.bottom - height));
        }

        SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        ApplyWindowChrome(hDlg);
        EnsureThemeFonts(hDlg);

        g_installs = *(std::vector<VivadoInstall>*)lParam;
        HWND hList = GetDlgItem(hDlg, IDC_VERSION_LIST);
        PopulateList(hList);

        if (!g_installs.empty())
        {
            SendMessageW(hList, LB_SETCURSEL, 0, 0);
            g_selectedIndex = 0;
        }

        g_oldListBoxProc = (WNDPROC)SetWindowLongPtrW(hList, GWLP_WNDPROC, (LONG_PTR)ListBoxSubclassProc);

        SetFocus(hList);
        return (INT_PTR)FALSE;
    }

    case WM_MEASUREITEM:
        return HandleMeasureItem(hDlg, lParam);

    case WM_DRAWITEM:
    {
        const DRAWITEMSTRUCT* dis = (const DRAWITEMSTRUCT*)lParam;
        if (dis->CtlID == IDC_VERSION_LIST)
        {
            int i = (int)dis->itemData;
            if (i >= 0 && i < (int)g_installs.size())
                DrawTwoLineItem(dis, g_installs[i].version, g_installs[i].path, false);
            return TRUE;
        }
        return FALSE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return DarkDialogColor(message, wParam, lParam);

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);

        if (id == IDC_VERSION_LIST && HIWORD(wParam) == LBN_DBLCLK)
        {
            HWND hList = GetDlgItem(hDlg, IDC_VERSION_LIST);
            int selIdx = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (selIdx >= 0)
            {
                int origIdx = (int)SendMessageW(hList, LB_GETITEMDATA, selIdx, 0);
                EndDialog(hDlg, origIdx + 1);
            }
            return TRUE;
        }

        if (id == IDCANCEL)
        {
            EndDialog(hDlg, 0);
            return TRUE;
        }
        break;
    }
    }

    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK AddPathDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
    {
        ApplyWindowChrome(hDlg);
        EnsureThemeFonts(hDlg);
        SubclassOwnerButton(hDlg, IDOK);
        SubclassOwnerButton(hDlg, IDCANCEL);
        SubclassOwnerButton(hDlg, IDC_BROWSE_BUTTON);
        return (INT_PTR)TRUE;
    }

    case WM_MEASUREITEM:
        return HandleMeasureItem(hDlg, lParam);

    case WM_DRAWITEM:
    {
        DrawOwnerButton((const DRAWITEMSTRUCT*)lParam);
        return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return DarkDialogColor(message, wParam, lParam);

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDOK)
        {
            HWND hEdit = GetDlgItem(hDlg, IDC_PATH_EDIT);
            wchar_t path[MAX_PATH] = {};
            GetWindowTextW(hEdit, path, MAX_PATH);

            if (path[0])
            {
                std::wstring error;
                auto installs = AddCustomPath(path, error);
                if (!error.empty())
                {
                    MessageBoxW(hDlg, error.c_str(), L"Cannot add path",
                                MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                g_installs = installs;
                EndDialog(hDlg, 1);
            }
            return TRUE;
        }

        if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, 0);
            return TRUE;
        }

        if (LOWORD(wParam) == IDC_BROWSE_BUTTON)
        {
            OPENFILENAMEW ofn = {};
            wchar_t path[MAX_PATH] = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = L"Select Vivado Installation Folder";

            if (GetOpenFileNameW(&ofn))
            {
                SetWindowTextW(GetDlgItem(hDlg, IDC_PATH_EDIT), path);
            }
            return TRUE;
        }
        break;
    }
    }

    return (INT_PTR)FALSE;
}

static std::vector<VivadoInstall> g_mainInstalls;
static std::vector<RecentProject> g_mainRecent;
static LaunchSettings g_launchSettings;
static std::wstring g_mainDefaultPath;

static void CenterMainDialog(HWND hDlg)
{
    RECT windowRect = {};
    if (!GetWindowRect(hDlg, &windowRect))
        return;

    POINT cursor = {};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = { sizeof(monitorInfo) };
    if (!GetMonitorInfoW(monitor, &monitorInfo))
        return;

    int width = windowRect.right - windowRect.left;
    int height = windowRect.bottom - windowRect.top;
    int x = monitorInfo.rcWork.left +
            ((monitorInfo.rcWork.right - monitorInfo.rcWork.left) - width) / 2;
    int y = monitorInfo.rcWork.top +
            ((monitorInfo.rcWork.bottom - monitorInfo.rcWork.top) - height) / 2;

    SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ShowMainPage(HWND hDlg, int page)
{
    const int projectControls[] = {
        IDC_PROJECT_LABEL, IDC_PROJECT_PATH, IDC_BROWSE_PROJECT,
        IDC_RECENT_LABEL, IDC_RECENT_PROJECTS, IDC_INSTALL_LABEL, IDC_INSTALL_LIST,
        IDC_REFRESH_INSTALLS, IDC_ADD_INSTALL, IDC_SET_DEFAULT,
        IDC_NO_LOG, IDC_NO_JOURNAL, IDC_ADDITIONAL_LABEL, IDC_EXTRA_ARGS,
        IDC_BIND_PROJECT, IDC_OPEN_PROJECT
    };
    const int diagnosticControls[] = {
        IDC_DIAGNOSTIC_LABEL, IDC_VIEW_LOG, IDC_OPEN_LOG_FOLDER, IDC_CLEAR_RECENT,
        IDC_OPEN_CONFIG_FOLDER, IDC_REGISTER_XPR, IDC_LOG_LABEL, IDC_LOG_PATH,
        IDC_CONFIG_LABEL, IDC_CONFIG_PATH
    };
    const int helpControls[] = { IDC_HELP_LABEL, IDC_HELP_TEXT };

    for (int id : projectControls) ShowWindow(GetDlgItem(hDlg, id), page == 0 ? SW_SHOW : SW_HIDE);
    for (int id : diagnosticControls) ShowWindow(GetDlgItem(hDlg, id), page == 1 ? SW_SHOW : SW_HIDE);
    for (int id : helpControls) ShowWindow(GetDlgItem(hDlg, id), page == 2 ? SW_SHOW : SW_HIDE);
}

static void PopulateMainInstallList(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_INSTALL_LIST);
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    g_mainDefaultPath = LoadDefaultInstallPath();
    wchar_t projectPath[32768] = {};
    GetWindowTextW(GetDlgItem(hDlg, IDC_PROJECT_PATH), projectPath, ARRAYSIZE(projectPath));
    std::wstring boundPath = LoadProjectInstallPath(projectPath);
    int defaultIndex = -1;

    for (size_t i = 0; i < g_mainInstalls.size(); ++i)
    {
        const auto& install = g_mainInstalls[i];
        std::wstring text = install.version + L"    " + install.path;
        int item = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)text.c_str());
        SendMessageW(hList, LB_SETITEMDATA, item, (LPARAM)i);
        if (!boundPath.empty() && SamePath(install.path, boundPath))
            defaultIndex = item;
    }

    if (defaultIndex >= 0)
        SendMessageW(hList, LB_SETCURSEL, defaultIndex, 0);
    else
    {
        // No project binding: preselect the default installation.
        for (size_t i = 0; i < g_mainInstalls.size(); ++i)
        {
            if (SamePath(g_mainInstalls[i].path, g_mainDefaultPath))
            {
                SendMessageW(hList, LB_SETCURSEL, (WPARAM)i, 0);
                break;
            }
        }
        if ((int)SendMessageW(hList, LB_GETCURSEL, 0, 0) < 0 && !g_mainInstalls.empty())
            SendMessageW(hList, LB_SETCURSEL, 0, 0);
    }
}

static void PopulateRecentList(HWND hDlg)
{
    HWND hList = GetDlgItem(hDlg, IDC_RECENT_PROJECTS);
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    g_mainRecent = LoadRecentProjects();
    for (size_t i = 0; i < g_mainRecent.size(); ++i)
    {
        std::wstring display = fs::path(g_mainRecent[i].path).filename().native();
        display += L"  (" + g_mainRecent[i].path + L")";
        int item = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)display.c_str());
        SendMessageW(hList, LB_SETITEMDATA, item, (LPARAM)i);
    }
}

static void SetProjectPath(HWND hDlg, const std::wstring& path)
{
    SetWindowTextW(GetDlgItem(hDlg, IDC_PROJECT_PATH), path.c_str());
}

static void OpenShellPath(HWND hDlg, const std::wstring& path)
{
    if (path.empty())
        return;
    HINSTANCE result = ShellExecuteW(hDlg, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32)
    {
        MessageBoxW(hDlg, L"Windows could not open this path.", L"Vivado Launcher",
                    MB_OK | MB_ICONWARNING);
    }
}

static bool OpenMainProject(HWND hDlg)
{
    wchar_t path[32768] = {};
    GetWindowTextW(GetDlgItem(hDlg, IDC_PROJECT_PATH), path, ARRAYSIZE(path));
    std::wstring projectPath = GetAbsolutePath(path);
    if (projectPath.empty() || !PathExists(projectPath))
    {
        MessageBoxW(hDlg, L"Please select an existing Vivado .xpr project.", L"Project not found",
                    MB_OK | MB_ICONWARNING);
        return false;
    }

    HWND hList = GetDlgItem(hDlg, IDC_INSTALL_LIST);
    int selected = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
    if (selected < 0)
    {
        MessageBoxW(hDlg, L"No valid Vivado installation is available.", L"Vivado Launcher",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    int index = (int)SendMessageW(hList, LB_GETITEMDATA, selected, 0);
    if (index < 0 || index >= (int)g_mainInstalls.size())
        return false;

    const auto& install = g_mainInstalls[index];
    std::wstring boundPath = LoadProjectInstallPath(projectPath);
    if (!boundPath.empty() && !SamePath(boundPath, install.path))
    {
        std::wstring boundVersion = L"a different Vivado version";
        for (const auto& inst : g_mainInstalls)
        {
            if (SamePath(inst.path, boundPath))
            {
                boundVersion = L"Vivado " + inst.version;
                break;
            }
        }
        std::wstring message = L"This project is bound to " + boundVersion +
                               L", but Vivado " + install.version + L" is selected.\n\nContinue anyway?";
        if (MessageBoxW(hDlg, message.c_str(), L"Vivado version mismatch",
                        MB_YESNO | MB_ICONWARNING) != IDYES)
            return false;
    }
    RememberRecentProject(projectPath, install.version);
    wchar_t extraArgs[32768] = {};
    GetWindowTextW(GetDlgItem(hDlg, IDC_EXTRA_ARGS), extraArgs, ARRAYSIZE(extraArgs));
    g_launchSettings.extraArgs = extraArgs;
    SaveLaunchSettings(g_launchSettings);
    if (!LaunchVivado(install.exePath.c_str(), projectPath.c_str(), g_launchSettings))
    {
        MessageBoxW(hDlg, L"Vivado could not be started. Check the installation path.",
                    L"Launch failed", MB_OK | MB_ICONERROR);
        return false;
    }
    EndDialog(hDlg, IDOK);
    return true;
}

INT_PTR CALLBACK MainDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        CenterMainDialog(hDlg);
        ApplyWindowChrome(hDlg);
        EnsureThemeFonts(hDlg);
        for (int id : { IDC_BROWSE_PROJECT, IDC_REFRESH_INSTALLS, IDC_ADD_INSTALL,
                        IDC_SET_DEFAULT, IDC_BIND_PROJECT, IDC_OPEN_PROJECT, IDCANCEL,
                        IDC_VIEW_LOG, IDC_OPEN_LOG_FOLDER, IDC_CLEAR_RECENT,
                        IDC_OPEN_CONFIG_FOLDER, IDC_REGISTER_XPR })
        {
            SubclassOwnerButton(hDlg, id);
        }
        HWND hNav = GetDlgItem(hDlg, IDC_NAV_LIST);
        for (const wchar_t* name : { L"Projects", L"Diagnosis", L"Help" })
            SendMessageW(hNav, LB_ADDSTRING, 0, (LPARAM)name);
        SendMessageW(hNav, LB_SETCURSEL, 0, 0);
        const wchar_t* initialPath = (const wchar_t*)lParam;
        if (initialPath && *initialPath)
            SetProjectPath(hDlg, initialPath);
        g_mainInstalls = DetectVivadoInstallations();
        g_launchSettings = LoadLaunchSettings();
        SetWindowTextW(GetDlgItem(hDlg, IDC_EXTRA_ARGS), g_launchSettings.extraArgs.c_str());
        SetWindowTextW(GetDlgItem(hDlg, IDC_LOG_PATH), GetLogFilePath().c_str());
        SetWindowTextW(GetDlgItem(hDlg, IDC_CONFIG_PATH), GetSettingsFilePath().c_str());
        SetWindowTextW(GetDlgItem(hDlg, IDC_HELP_TEXT),
                       L"vivlauncher help\r\n\r\n"
                       L"Projects\r\n"
                       L"Pick an .xpr file or double-click one under Recent projects.\r\n"
                       L"Select a Vivado installation on the right, then click Open project.\r\n"
                       L"Bind project stores the selected installation for this project only.\r\n"
                       L"Set default chooses the fallback installation for new projects.\r\n\r\n"
                       L"Launch options\r\n"
                       L"No log and No journal add -nolog and -nojournal.\r\n"
                       L"Additional arguments are appended to the Vivado command line.\r\n"
                       L"Settings are saved under the vivlauncher AppData folder.\r\n\r\n"
                       L"Diagnosis\r\n"
                       L"View log opens the latest launch record. The log includes paths, arguments,\r\n"
                       L"working directory, result, and Windows error code when startup fails.\r\n\r\n"
                       L"Command line\r\n"
                       L"vivlauncher.exe project.xpr\r\n"
                       L"vivlauncher.exe --list\r\n"
                       L"vivlauncher.exe --add C:\\Vivado\\2025.2\\Vivado");
        PopulateMainInstallList(hDlg);
        PopulateRecentList(hDlg);
        ShowMainPage(hDlg, 0);
        return TRUE;
    }
    case WM_MEASUREITEM:
        return HandleMeasureItem(hDlg, lParam);
    case WM_DRAWITEM:
    {
        const DRAWITEMSTRUCT* dis = (const DRAWITEMSTRUCT*)lParam;
        switch (dis->CtlID)
        {
        case IDC_NAV_LIST:
            DrawNavItem(dis);
            return TRUE;
        case IDC_RECENT_PROJECTS:
        {
            int i = (int)dis->itemData;
            if (i >= 0 && i < (int)g_mainRecent.size())
            {
                fs::path projectPath(g_mainRecent[i].path);
                DrawTwoLineItem(dis, projectPath.filename().native(), g_mainRecent[i].path, false);
            }
            return TRUE;
        }
        case IDC_INSTALL_LIST:
        {
            int i = (int)dis->itemData;
            if (i >= 0 && i < (int)g_mainInstalls.size())
            {
                bool isDefault = SamePath(g_mainInstalls[i].path, g_mainDefaultPath);
                DrawTwoLineItem(dis, g_mainInstalls[i].version, g_mainInstalls[i].path, isDefault);
            }
            return TRUE;
        }
        case IDC_NO_LOG:
            DrawOwnerCheckbox(dis, g_launchSettings.noLog);
            return TRUE;
        case IDC_NO_JOURNAL:
            DrawOwnerCheckbox(dis, g_launchSettings.noJournal);
            return TRUE;
        default:
            DrawOwnerButton(dis);
            return TRUE;
        }
    }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return DarkDialogColor(message, wParam, lParam);
    case WM_DESTROY:
        DestroyThemeFonts();
        return FALSE;
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDC_NAV_LIST && HIWORD(wParam) == LBN_SELCHANGE)
        {
            ShowMainPage(hDlg, (int)SendMessageW((HWND)lParam, LB_GETCURSEL, 0, 0));
            return TRUE;
        }
        if ((id == IDC_NO_LOG || id == IDC_NO_JOURNAL) && HIWORD(wParam) == BN_CLICKED)
        {
            if (id == IDC_NO_LOG)
                g_launchSettings.noLog = !g_launchSettings.noLog;
            else
                g_launchSettings.noJournal = !g_launchSettings.noJournal;
            InvalidateRect((HWND)lParam, nullptr, TRUE);
            return TRUE;
        }
        if (id == IDC_BROWSE_PROJECT)
        {
            OPENFILENAMEW ofn = {};
            wchar_t path[MAX_PATH] = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFilter = L"Vivado Project (*.xpr)\0*.xpr\0All Files (*.*)\0*.*\0\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = ARRAYSIZE(path);
            if (GetOpenFileNameW(&ofn))
            {
                SetProjectPath(hDlg, path);
                PopulateMainInstallList(hDlg);
            }
            return TRUE;
        }
        if (id == IDC_RECENT_PROJECTS &&
            (HIWORD(wParam) == LBN_SELCHANGE || HIWORD(wParam) == LBN_DBLCLK))
        {
            int item = (int)SendMessageW((HWND)lParam, LB_GETCURSEL, 0, 0);
            if (item >= 0)
            {
                int index = (int)SendMessageW((HWND)lParam, LB_GETITEMDATA, item, 0);
                if (index >= 0 && index < (int)g_mainRecent.size())
                {
                    SetProjectPath(hDlg, g_mainRecent[index].path);
                    PopulateMainInstallList(hDlg);
                    if (HIWORD(wParam) == LBN_DBLCLK)
                        OpenMainProject(hDlg);
                }
            }
            return TRUE;
        }
        if (id == IDC_REFRESH_INSTALLS)
        {
            g_mainInstalls = DetectVivadoInstallations();
            PopulateMainInstallList(hDlg);
            return TRUE;
        }
        if (id == IDC_SET_DEFAULT)
        {
            HWND hList = GetDlgItem(hDlg, IDC_INSTALL_LIST);
            int item = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (item >= 0)
            {
                int index = (int)SendMessageW(hList, LB_GETITEMDATA, item, 0);
                if (index >= 0 && index < (int)g_mainInstalls.size())
                {
                    SaveDefaultInstallPath(g_mainInstalls[index].path);
                    MessageBoxW(hDlg, L"The selected Vivado version is now the default.",
                                L"Vivado Launcher", MB_OK | MB_ICONINFORMATION);
                }
            }
            return TRUE;
        }
        if (id == IDC_BIND_PROJECT)
        {
            wchar_t path[32768] = {};
            GetWindowTextW(GetDlgItem(hDlg, IDC_PROJECT_PATH), path, ARRAYSIZE(path));
            std::wstring projectPath = GetAbsolutePath(path);
            HWND hList = GetDlgItem(hDlg, IDC_INSTALL_LIST);
            int item = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (projectPath.empty() || !PathExists(projectPath) || item < 0)
            {
                MessageBoxW(hDlg, L"Select an existing project and Vivado version first.",
                            L"Vivado Launcher", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            int index = (int)SendMessageW(hList, LB_GETITEMDATA, item, 0);
            if (index >= 0 && index < (int)g_mainInstalls.size() &&
                SaveProjectInstallPath(projectPath, g_mainInstalls[index].path))
            {
                MessageBoxW(hDlg, L"This project will use the selected Vivado version next time.",
                            L"Vivado Launcher", MB_OK | MB_ICONINFORMATION);
            }
            return TRUE;
        }
        if (id == IDC_REGISTER_XPR)
        {
            bool registered = RegisterXprFileAssociation();
            MessageBoxW(hDlg, registered ? L".xpr files are now associated with vivlauncher."
                                         : L"Could not register the .xpr file association.",
                        L"Vivado Launcher", MB_OK | (registered ? MB_ICONINFORMATION : MB_ICONERROR));
            return TRUE;
        }
        if (id == IDC_VIEW_LOG)
        {
            auto logPath = GetLogFilePath();
            if (!PathExists(logPath))
            {
                MessageBoxW(hDlg, L"No launch log has been created yet.", L"Vivado Launcher",
                            MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                OpenShellPath(hDlg, logPath);
            }
            return TRUE;
        }
        if (id == IDC_OPEN_LOG_FOLDER || id == IDC_OPEN_CONFIG_FOLDER)
        {
            auto filePath = id == IDC_OPEN_LOG_FOLDER ? GetLogFilePath() : GetSettingsFilePath();
            OpenShellPath(hDlg, fs::path(filePath).parent_path().native());
            return TRUE;
        }
        if (id == IDC_CLEAR_RECENT)
        {
            if (MessageBoxW(hDlg, L"Clear the recent project list?", L"Vivado Launcher",
                            MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                if (ClearRecentProjects())
                {
                    PopulateRecentList(hDlg);
                    MessageBoxW(hDlg, L"Recent projects cleared.", L"Vivado Launcher",
                                MB_OK | MB_ICONINFORMATION);
                }
            }
            return TRUE;
        }
        if (id == IDC_ADD_INSTALL)
        {
            if (DialogBoxW(hInst, MAKEINTRESOURCE(IDD_ADD_PATH), hDlg, AddPathDialogProc) > 0)
            {
                g_mainInstalls = DetectVivadoInstallations();
                PopulateMainInstallList(hDlg);
            }
            return TRUE;
        }
        if (id == IDC_OPEN_PROJECT || (id == IDOK && HIWORD(wParam) == BN_CLICKED))
        {
            OpenMainProject(hDlg);
            return TRUE;
        }
        if (id == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}
