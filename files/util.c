// ============================================================================
//  util.c — window identity helpers, ignore-list classification, monitor /
//  taskbar geometry helpers, and small process-lookup utilities shared by
//  every other module.
// ============================================================================

#include "wm_common.h"

void GetWindowClassSafe(HWND h, wchar_t* buf, int buflen) {
    buf[0] = 0;
    GetClassNameW(h, buf, buflen);
}

void GetWindowProcessNameSafe(HWND h, wchar_t* buf, int buflen) {
    buf[0] = 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!pid) return;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return;

    wchar_t full[MAX_PATH];
    DWORD len = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, full, &len)) {
        wchar_t* slash = wcsrchr(full, L'\\');
        wcsncpy(buf, slash ? slash + 1 : full, buflen - 1);
        buf[buflen - 1] = 0;
    }
    CloseHandle(hProc);
}

// ============================================================================
//  Ignore-list helpers (mirrors IsShellClass / IsPopupClass / etc.)
// ============================================================================

BOOL StrEqI(const wchar_t* a, const wchar_t* b) {
    return _wcsicmp(a, b) == 0;
}

BOOL IsShellClass(const wchar_t* c) {
    for (int i = 0; i < g_ignoreClassesCount; i++) {
        if (StrEqI(c, g_ignoreClasses[i])) return TRUE;
    }
    return FALSE;
}

BOOL IsPopupClass(const wchar_t* c) {
    static const wchar_t* list[] = {
        L"#32768", L"MozillaDropShadowWindowClass", L"MozillaPopupWindowClass",
        L"Xaml_WindowedPopupClass", L"Windows.UI.Core.CoreWindow",
        L"Microsoft.UI.Content.PopupWindowSiteBridge", L"AutoSuggestBoxPopupWindowClass",
        L"tooltips_class32", L"SysShadow"
    };
    for (int i = 0; i < 9; i++) if (StrEqI(c, list[i])) return TRUE;
    return FALSE;
}

static BOOL IsIgnoredUtilityProcess(const wchar_t* p) {
    for (int i = 0; i < g_ignoreProcessesCount; i++) {
        if (StrEqI(p, g_ignoreProcesses[i])) return TRUE;
    }
    return FALSE;
}

BOOL ShouldIgnoreNewWindowProcess(const wchar_t* p) {
    for (int i = 0; i < g_ignoreNewWindowProcessesCount; i++) {
        if (StrEqI(p, g_ignoreNewWindowProcesses[i])) return TRUE;
    }
    return FALSE;
}

BOOL IsFirefoxWindow(HWND h) {
    wchar_t p[MAX_PATH];
    GetWindowProcessNameSafe(h, p, MAX_PATH);
    return StrEqI(p, L"firefox.exe");
}

BOOL ShouldIgnoreFocusWindow(HWND h) {
    if (!h || !IsWindow(h)) return TRUE;
    if (!IsWindowVisible(h)) return TRUE;

    wchar_t cls[64];
    GetWindowClassSafe(h, cls, 64);
    if (IsShellClass(cls) || IsPopupClass(cls)) return TRUE;

    LONG_PTR exStyle = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) || (exStyle & WS_EX_NOACTIVATE)) return TRUE;

    LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    HWND owner = GetWindow(h, GW_OWNER);
    if (owner && (style & WS_POPUP)) return TRUE;

    wchar_t proc[MAX_PATH];
    GetWindowProcessNameSafe(h, proc, MAX_PATH);
    if (IsIgnoredUtilityProcess(proc)) return TRUE;

    return FALSE;
}

BOOL CanMoveToDesktop(HWND h) {
    if (!h) return FALSE;
    wchar_t cls[64], proc[MAX_PATH];
    GetWindowClassSafe(h, cls, 64);
    if (IsShellClass(cls) || IsPopupClass(cls)) return FALSE;
    GetWindowProcessNameSafe(h, proc, MAX_PATH);
    if (IsIgnoredUtilityProcess(proc)) return FALSE;
    return TRUE;
}

BOOL IsRealWindow(HWND h) {
    if (!h) return FALSE;
    wchar_t cls[64], proc[MAX_PATH];
    GetWindowClassSafe(h, cls, 64);
    if (IsShellClass(cls)) return FALSE;
    GetWindowProcessNameSafe(h, proc, MAX_PATH);
    if (IsIgnoredUtilityProcess(proc)) return FALSE;

    // Owned WS_POPUP windows are transient overlays spawned by another
    // window (Telegram media viewer, FastStone fullscreen, video player
    // overlays, etc.) — not independent top-level apps. The same signal
    // ShouldIgnoreFocusWindow uses to skip them for focus-follow is reused
    // here so maximize/fullscreen/close/minimize/snap don't act on them
    // either; none of those operations make sense on a transient popup that
    // the owning app controls the lifetime of.
    LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
    HWND owner = GetWindow(h, GW_OWNER);
    if (owner && (style & WS_POPUP)) return FALSE;

    return TRUE;
}

// ============================================================================
//  Monitor / taskbar helpers
// ============================================================================

void GetMonitorWorkAreaForWindow(HWND h, RECT* out) {
    HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    GetMonitorInfoW(mon, &mi);
    *out = mi.rcWork;
}

void GetMonitorBoundsForWindow(HWND h, RECT* out) {
    HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    GetMonitorInfoW(mon, &mi);
    *out = mi.rcMonitor;
}

static HWND GetTaskbarForMonitor(HMONITOR mon) {
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, (MONITORINFO*)&mi);

    if (mi.dwFlags & MONITORINFOF_PRIMARY)
        return FindWindowW(L"Shell_TrayWnd", NULL);

    // Secondary monitor: find the Shell_SecondaryTrayWnd whose center falls
    // inside this monitor's rect.
    HWND found = NULL;
    HWND tray = NULL;
    while ((tray = FindWindowExW(NULL, tray, L"Shell_SecondaryTrayWnd", NULL)) != NULL) {
        RECT r;
        if (!GetWindowRect(tray, &r)) continue;
        LONG cx = (r.left + r.right) / 2;
        LONG cy = (r.top + r.bottom) / 2;
        if (cx >= mi.rcMonitor.left && cx < mi.rcMonitor.right &&
            cy >= mi.rcMonitor.top && cy < mi.rcMonitor.bottom) {
            found = tray;
            break;
        }
    }
    return found;
}

void HideTaskbarForWindow(HWND h) {
    HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    HWND tb = GetTaskbarForMonitor(mon);
    if (tb) ShowWindow(tb, SW_HIDE);
}

void ShowTaskbarForWindow(HWND h) {
    HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    HWND tb = GetTaskbarForMonitor(mon);
    if (tb) ShowWindow(tb, SW_SHOW);
}

static void GetVisibleFrameOffsets(HWND h, LONG* leftO, LONG* topO, LONG* rightO, LONG* bottomO) {
    *leftO = *topO = *rightO = *bottomO = 0;

    RECT wr;
    if (!GetWindowRect(h, &wr)) return;

    RECT vr;
    HRESULT hr = DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &vr, sizeof(vr));
    if (FAILED(hr)) return;

    *leftO   = max(0L, vr.left - wr.left);
    *topO    = max(0L, vr.top - wr.top);
    *rightO  = max(0L, wr.right - vr.right);
    *bottomO = max(0L, wr.bottom - vr.bottom);
}

void MoveWindowVisibleRect(HWND h, LONG l, LONG t, LONG r, LONG b) {
    LONG lo, to, ro, bo;
    GetVisibleFrameOffsets(h, &lo, &to, &ro, &bo);
    MoveWindow(h, l - lo, t - to, (r - l) + lo + ro, (b - t) + to + bo, TRUE);
}

BOOL RestoreWindowDirectlyToRect(HWND h, LONG l, LONG t, LONG r, LONG b) {
    WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
    if (!GetWindowPlacement(h, &wp)) return FALSE;
    wp.showCmd = SW_SHOWNORMAL;
    wp.rcNormalPosition.left = l;
    wp.rcNormalPosition.top = t;
    wp.rcNormalPosition.right = r;
    wp.rcNormalPosition.bottom = b;
    return SetWindowPlacement(h, &wp);
}

// ============================================================================
//  Small process-lookup utilities
// ============================================================================

BOOL FileExistsW(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

typedef struct { const wchar_t* targetExe; HWND result; } FindWinCtx;

static BOOL CALLBACK FindWindowByProcessProc(HWND h, LPARAM lParam) {
    FindWinCtx* ctx = (FindWinCtx*)lParam;
    if (!IsWindowVisible(h)) return TRUE;
    if (GetWindow(h, GW_OWNER) != NULL) return TRUE; // skip owned/child-like windows

    wchar_t proc[MAX_PATH];
    GetWindowProcessNameSafe(h, proc, MAX_PATH);
    if (StrEqI(proc, ctx->targetExe)) {
        ctx->result = h;
        return FALSE;
    }
    return TRUE;
}

HWND FindMainWindowByProcessName(const wchar_t* exeName) {
    FindWinCtx ctx = { exeName, NULL };
    EnumWindows(FindWindowByProcessProc, (LPARAM)&ctx);
    return ctx.result;
}
