// ============================================================================
//  tray.c — system tray icon, its right-click menu, and the short-lived
//  startup timer that hides the RoundedTB helper window once it appears.
// ============================================================================

#include "wm_common.h"

void AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
    nid.hWnd = hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAYICON;
    // Load the icon we embedded via wm.rc — falls back to the generic
    // Windows icon if somehow the resource isn't found.
    nid.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APPICON));
    if (!nid.hIcon) nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcsncpy_s(nid.szTip, ARRAYSIZE(nid.szTip), L"wm — window manager", _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
    nid.hWnd = hwnd;
    nid.uID = TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_VIEWLOG, L"View log");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(hwnd); // required so the menu dismisses correctly
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

// ============================================================================
//  RoundedTB auto-hide on startup (short-lived timer, self-cancels)
// ============================================================================

void TryHideRoundedTB(HWND hiddenWnd) {
    static int tries = 0;
    tries++;
    if (tries > 60) { KillTimer(hiddenWnd, TIMER_ROUNDEDTB_HIDE); return; }

    HWND found = FindMainWindowByProcessName(L"RoundedTB.exe");
    if (found) {
        ShowWindow(found, SW_HIDE);
        KillTimer(hiddenWnd, TIMER_ROUNDEDTB_HIDE);
    }
}
