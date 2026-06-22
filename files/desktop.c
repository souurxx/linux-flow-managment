// ============================================================================
//  desktop.c — VirtualDesktopAccessor.dll loading + virtual desktop
//  switching/move, and the snap-left/snap-right window action.
// ============================================================================

#include "wm_common.h"

typedef int (WINAPI *PFN_GoToDesktopNumber)(int);
typedef int (WINAPI *PFN_MoveWindowToDesktopNumber)(HWND, int);

HMODULE g_vdaModule = NULL;
BOOL    g_vdaAvailable = FALSE;
static PFN_GoToDesktopNumber         g_fnGoToDesktop = NULL;
static PFN_MoveWindowToDesktopNumber g_fnMoveWindowToDesktop = NULL;

static BOOL IsWindows11OrLater(void) {
    // RtlGetVersion is the reliable way to get the real build number —
    // GetVersionEx lies to apps that don't have a compatibility manifest.
    typedef LONG (WINAPI *PFN_RtlGetVersion)(OSVERSIONINFOEXW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return FALSE;
    PFN_RtlGetVersion fn = (PFN_RtlGetVersion)GetProcAddress(ntdll, "RtlGetVersion");
    if (!fn) return FALSE;
    OSVERSIONINFOEXW vi = { sizeof(vi) };
    fn(&vi);
    // Windows 11 starts at build 22000
    return vi.dwBuildNumber >= 22000;
}

void LoadVirtualDesktopAccessor(void) {
    // Pick the right DLL for the running OS:
    //   Win11 (build >= 22000): VirtualDesktopAccessorwin11.dll first
    //   Win10 (build < 22000):  VirtualDesktopAccessor.dll first
    // Always fall back to the other name in case only one file is present.
    const wchar_t* primary   = IsWindows11OrLater()
                                   ? L"VirtualDesktopAccessorwin11.dll"
                                   : L"VirtualDesktopAccessor.dll";
    const wchar_t* secondary = IsWindows11OrLater()
                                   ? L"VirtualDesktopAccessor.dll"
                                   : L"VirtualDesktopAccessorwin11.dll";
    const wchar_t* candidates[] = { primary, secondary };

    wchar_t dllPath[MAX_PATH];
    for (int i = 0; i < 2; i++) {
        swprintf(dllPath, MAX_PATH, L"%ls\\%ls", g_exeDir, candidates[i]);
        g_vdaModule = LoadLibraryW(dllPath);
        if (g_vdaModule) {
            LogMsg(L"Loaded virtual desktop DLL: %ls", candidates[i]);
            break;
        }
    }

    if (!g_vdaModule) {
        wchar_t msg[512];
        swprintf(msg, 512,
            L"Could not load VirtualDesktopAccessor.dll from:\n%ls\n\n"
            L"Virtual desktop hotkeys (Alt+1/2/3/4) will be disabled.\n"
            L"Place VirtualDesktopAccessor.dll next to wm.exe and restart.",
            g_exeDir);
        LogMsg(L"VDA load failed from '%ls'. Virtual desktop hotkeys disabled.", g_exeDir);
        MessageBoxW(NULL, msg, L"wm — VDA not found", MB_OK | MB_ICONWARNING);
        return;
    }

    g_fnGoToDesktop = (PFN_GoToDesktopNumber)GetProcAddress(g_vdaModule, "GoToDesktopNumber");
    g_fnMoveWindowToDesktop = (PFN_MoveWindowToDesktopNumber)GetProcAddress(g_vdaModule, "MoveWindowToDesktopNumber");

    if (!g_fnGoToDesktop || !g_fnMoveWindowToDesktop) {
        wchar_t msg2[512];
        swprintf(msg2, 512,
            L"VirtualDesktopAccessor.dll loaded but the expected exports\n"
            L"(GoToDesktopNumber / MoveWindowToDesktopNumber) were not found.\n\n"
            L"The DLL may be the wrong version for your Windows build.\n"
            L"Virtual desktop hotkeys will be disabled.");
        LogMsg(L"VDA exports not found — wrong DLL version?");
        MessageBoxW(NULL, msg2, L"wm — VDA export error", MB_OK | MB_ICONWARNING);
        FreeLibrary(g_vdaModule);
        g_vdaModule = NULL;
        return;
    }

    g_vdaAvailable = TRUE;
}

// ============================================================================
//  Virtual desktops
// ============================================================================

void GoToDesktop(int n) {
    if (!g_vdaAvailable) { LogMsg(L"GoToDesktop(%d) skipped: VirtualDesktopAccessor.dll not loaded.", n); return; }
    g_fnGoToDesktop(n);
}

static HWND GetWindowUnderCursorAncestor(void) {
    POINT pt;
    GetCursorPos(&pt);
    HWND h = WindowFromPoint(pt);
    if (!h) h = GetForegroundWindow();
    if (!h) return NULL;
    return GetAncestor(h, GA_ROOT);
}

void MoveWindowToDesktop(int n, BOOL alsoGo) {
    if (!g_vdaAvailable) { LogMsg(L"MoveWindowToDesktop(%d) skipped: DLL not loaded.", n); return; }

    HWND h = GetWindowUnderCursorAncestor();
    if (!CanMoveToDesktop(h)) return;

    // FALSE: we are not restoring state here, just moving — don't leave
    // the node intact waiting for a RestoreWindowState that never comes.
    ClearWindowState(h, FALSE);
    g_fnMoveWindowToDesktop(h, n);
    if (alsoGo) GoToDesktop(n);
}

// ============================================================================
//  Snap left / right
// ============================================================================

void SnapTo(HWND h, BOOL left) {
    if (!h) return;

    WSNode* n = FindVerifiedNode(h);
    if (n && n->altState == ALTWSTATE_FULLSCREEN && IsFirefoxWindow(h)) {
        keybd_event(VK_F11, 0, 0, MARK_INJECTED);
        keybd_event(VK_F11, 0, KEYEVENTF_KEYUP, MARK_INJECTED);
        Sleep(150);
    }

    ClearWindowState(h, FALSE);

    RECT wa;
    GetMonitorWorkAreaForWindow(h, &wa);
    LONG w = wa.right - wa.left;

    LONG l = left ? wa.left : wa.left + (w / 2);
    LONG t = wa.top;
    LONG r = left ? wa.left + (w / 2) : wa.right;
    LONG b = wa.bottom;

    if (IsZoomed(h) || IsIconic(h))
        RestoreWindowDirectlyToRect(h, l, t, r, b);

    MoveWindowVisibleRect(h, l, t, r, b);
}
