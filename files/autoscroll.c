/*
 * autoscroll.c
 * Mouse Button 4 → native middle-click auto-scroll
 * Build: gcc autoscroll.c -o autoscroll.exe -luser32 -lshell32 -mwindows
 */

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#define WM_APP_TRAY  (WM_APP + 1)
#define ID_TRAY_ICON  1
#define ID_TRAY_EXIT  2

static HHOOK             g_hook  = NULL;
static NOTIFYICONDATAW   g_nid   = {0};
static HANDLE            g_mutex = NULL;
static BOOL              g_injecting = FALSE;

/* ── Hook ────────────────────────────────────────────────────────────────── */

static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        MSLLHOOKSTRUCT *ms = (MSLLHOOKSTRUCT *)lParam;

        /* Block both down and up of MB4 so nothing leaks through */
        if (!g_injecting &&
            (wParam == WM_XBUTTONDOWN || wParam == WM_XBUTTONUP) &&
            HIWORD(ms->mouseData) == XBUTTON1)
        {
            if (wParam == WM_XBUTTONDOWN) {
                g_injecting = TRUE;
                INPUT input[2] = {0};
                input[0].type       = INPUT_MOUSE;
                input[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
                input[1].type       = INPUT_MOUSE;
                input[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
                SendInput(2, input, sizeof(INPUT));
                g_injecting = FALSE;
            }
            return 1; /* block original MB4 down and up */
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

/* ── Tray ────────────────────────────────────────────────────────────────── */

static void TrayAdd(HWND hwnd)
{
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = ID_TRAY_ICON;
    g_nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon            = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    lstrcpyW(g_nid.szTip, L"AutoScroll - right-click to exit");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void TrayRemove(void)
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

static void ShowTrayMenu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
}

/* ── Cleanup ─────────────────────────────────────────────────────────────── */

static void Cleanup(void)
{
    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
    }
    TrayRemove();
    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
        g_mutex = NULL;
    }
}

/* ── Window proc ─────────────────────────────────────────────────────────── */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_APP_TRAY:
            if (lParam == WM_RBUTTONUP)
                ShowTrayMenu(hwnd);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_TRAY_EXIT)
                DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            Cleanup();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ── Entry ───────────────────────────────────────────────────────────────── */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd; (void)nShow;

    g_mutex = CreateMutexW(NULL, TRUE, L"AutoScrollMutex");
    if (!g_mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_mutex) CloseHandle(g_mutex);
        return 0;
    }

    WNDCLASSW wc     = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"AutoScrollWnd";
    if (!RegisterClassW(&wc)) { Cleanup(); return 1; }

    HWND hwnd = CreateWindowExW(0, L"AutoScrollWnd", L"AutoScroll",
                                0, 0, 0, 0, 0,
                                HWND_MESSAGE, NULL, hInstance, NULL);
    if (!hwnd) { Cleanup(); return 1; }

    TrayAdd(hwnd);

    g_hook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);
    if (!g_hook) { Cleanup(); return 1; }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
