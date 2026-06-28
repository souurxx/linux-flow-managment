// ============================================================================
//  hotkeys.c — the global Alt+<key> / Ctrl+<key> keyboard hook, the action
//  table it dispatches into, and every per-action handler (maximize, fake
//  fullscreen, close/minimize, app launchers, new-window spawn-under-mouse).
// ============================================================================

#include "wm_common.h"

HHOOK g_keyboardHook = NULL;

// ============================================================================
//  Maximize toggle (Alt+W) / fake fullscreen toggle (Alt+V) / escape
// ============================================================================

static void ToggleMaximize(void) {
    HWND h = GetForegroundWindow();
    if (!h || !IsRealWindow(h)) return;

    if (IsIconic(h)) {
        // Recover from a broken native unminimize state, same as the AHK original.
        ShowWindow(h, SW_RESTORE);
        Sleep(100);
        ShowWindow(h, SW_MAXIMIZE);
        WSNode* n = GetOrCreateNode(h);
        n->altState = ALTWSTATE_MAXIMIZED;
        SyncWindowLayering(h);
        return;
    }

    WSNode* n = FindVerifiedNode(h);
    int state = n ? n->altState : ALTWSTATE_NORMAL;

    if (state == ALTWSTATE_FULLSCREEN) return; // owned by Alt+V

    if (state == ALTWSTATE_MAXIMIZED && !IsZoomed(h)) state = ALTWSTATE_NORMAL;

    if (state == ALTWSTATE_NORMAL) {
        RECT r;
        GetWindowRect(h, &r);
        n = GetOrCreateNode(h);
        n->hasPos    = TRUE;
        n->x = r.left; n->y = r.top;
        n->w = r.right - r.left; n->h = r.bottom - r.top;
        n->wasTopmost = (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        n->style   = GetWindowLongPtrW(h, GWL_STYLE);
        n->exStyle = GetWindowLongPtrW(h, GWL_EXSTYLE);
        ShowWindow(h, SW_MAXIMIZE);
        n->altState = ALTWSTATE_MAXIMIZED;
        SyncWindowLayering(h);
        return;
    }

    // state == MAXIMIZED -> restore to normal
    ShowWindow(h, SW_RESTORE);
    if (n) { n->altState = ALTWSTATE_NORMAL; n->hasPos = FALSE; }
    SyncWindowLayering(h);
}

static void ToggleFakeFullscreen(void) {
    HWND h = GetForegroundWindow();
    if (!h || !IsRealWindow(h)) return;

    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);

    WSNode* n = GetOrCreateNode(h);
    int state = n->altState;

    if (IsFirefoxWindow(h)) {
        LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
        BOOL actuallyFullscreen = !(style & WS_CAPTION) && !(style & WS_THICKFRAME);
        if (actuallyFullscreen && state != ALTWSTATE_FULLSCREEN)  state = ALTWSTATE_FULLSCREEN;
        else if (!actuallyFullscreen && state == ALTWSTATE_FULLSCREEN) state = ALTWSTATE_NORMAL;
    }

    if (state == ALTWSTATE_FULLSCREEN) {
        if (IsFirefoxWindow(h)) {
            // Lift Alt key temporarily so Firefox receives a clean F11.
            keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, MARK_INJECTED);
            Sleep(20);
            keybd_event(VK_F11, 0, 0,               MARK_INJECTED);
            keybd_event(VK_F11, 0, KEYEVENTF_KEYUP, MARK_INJECTED);
            Sleep(20);
            keybd_event(VK_MENU, 0, 0,               MARK_INJECTED);
            Sleep(120);
        }
        RestoreWindowState(h);
        return;
    }

    // ── Entering fake fullscreen ──────────────────────────────────────────
    if (IsFirefoxWindow(h)) {
        // Lift Alt key temporarily so Firefox receives a clean F11.
        keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, MARK_INJECTED);
        Sleep(20);
        keybd_event(VK_F11, 0, 0,               MARK_INJECTED);
        keybd_event(VK_F11, 0, KEYEVENTF_KEYUP, MARK_INJECTED);
        Sleep(20);
        keybd_event(VK_MENU, 0, 0,               MARK_INJECTED);
        Sleep(150);
        n->altState = ALTWSTATE_FULLSCREEN;
        SyncWindowLayering(h);
        HideTaskbarForWindow(h);
        return;
    }

    RECT mb;
    GetMonitorBoundsForWindow(h, &mb);

    if (!n->hasPos) {
        RECT r;
        GetWindowRect(h, &r);
        n->hasPos    = TRUE;
        n->x = r.left; n->y = r.top;
        n->w = r.right - r.left; n->h = r.bottom - r.top;
        n->wasTopmost = (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        n->style   = GetWindowLongPtrW(h, GWL_STYLE);
        n->exStyle = GetWindowLongPtrW(h, GWL_EXSTYLE);
    }

    LONG_PTR style    = GetWindowLongPtrW(h, GWL_STYLE);
    LONG_PTR newStyle = style & ~WS_CAPTION;

    WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
    GetWindowPlacement(h, &wp);
    wp.showCmd          = SW_SHOWNORMAL;
    wp.rcNormalPosition = mb;

    SetWindowLongPtrW(h, GWL_STYLE, newStyle);
    SetWindowPlacement(h, &wp);

    SetWindowPos(h, NULL, mb.left, mb.top, mb.right - mb.left, mb.bottom - mb.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED |
                 SWP_NOREDRAW | SWP_NOCOPYBITS);

    RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

    n->altState = ALTWSTATE_FULLSCREEN;
    SyncWindowLayering(h);
    HideTaskbarForWindow(h);
}

static void EscapeFakeFullscreen(void) {
    HWND h = GetForegroundWindow();
    if (!h) return;
    WSNode* n = FindVerifiedNode(h);
    if (n && n->altState == ALTWSTATE_FULLSCREEN)
        RestoreWindowState(h);
}

// ============================================================================
//  Misc window actions
// ============================================================================

static void CloseActiveWindow(void) {
    HWND h = GetForegroundWindow();
    if (!h || !IsRealWindow(h)) return;

    // Bug #4 fix: if the window is in fake-fullscreen we must restore the
    // taskbar BEFORE posting WM_CLOSE, because once the window is gone there
    // is nothing left to derive the monitor from.  The old code called
    // ClearWindowState(h, TRUE) which left the node intact, then checked for
    // ALTWSTATE_FULLSCREEN and called RestoreWindowState — but RestoreWindowState
    // calls ShowTaskbarForWindow internally, so that path was actually correct.
    // The real gap was: ClearWindowState(allowRestore=TRUE) returns early
    // (leaving the node) only when altState==FULLSCREEN AND hasPos is set.
    // If hasPos was FALSE (e.g. Firefox fake-fullscreen which never saves pos),
    // ClearWindowState removed the node, the FindVerifiedNode below returned
    // NULL, and the taskbar stayed hidden.
    //
    // Fixed approach: check fullscreen state first, call RestoreWindowState
    // (which handles ShowTaskbarForWindow unconditionally) before the close.
    WSNode* n = FindVerifiedNode(h);
    if (n && n->altState == ALTWSTATE_FULLSCREEN) {
        RestoreWindowState(h);  // shows taskbar, restores styles, removes node
    } else {
        ClearWindowState(h, FALSE);
    }

    PostMessageW(h, WM_CLOSE, 0, 0);
}

static void MinimizeActiveWindow(void) {
    HWND h = GetForegroundWindow();
    if (!h || !IsRealWindow(h)) return;
    ClearWindowState(h, TRUE);
    ShowWindow(h, SW_MINIMIZE);
}

static void ShowDebugInfo(void) {
    HWND h = GetForegroundWindow();
    if (!h) { MessageBoxW(NULL, L"No active window.", L"Debug", MB_OK); return; }

    wchar_t title[256], cls[64], proc[MAX_PATH];
    GetWindowTextW(h, title, 256);
    GetWindowClassSafe(h, cls, 64);
    GetWindowProcessNameSafe(h, proc, MAX_PATH);

    LONG_PTR style   = GetWindowLongPtrW(h, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtrW(h, GWL_EXSTYLE);
    int minMax = IsZoomed(h) ? 1 : (IsIconic(h) ? -1 : 0);

    wchar_t info[1024];
    swprintf(info, 1024,
        L"Title: %ls\nClass: %ls\nProcess: %ls\nHwnd: 0x%p\nMinMax: %d\n"
        L"Style: 0x%08IX\nExStyle: 0x%08IX",
        title, cls, proc, h, minMax, (intptr_t)style, (intptr_t)exStyle);

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        size_t bytes = (wcslen(info) + 1) * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem) {
            void* p = GlobalLock(mem);
            memcpy(p, info, bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        }
        CloseClipboard();
    }

    MessageBoxW(NULL, info, L"Debug Info (copied to clipboard)", MB_OK);
}

// ============================================================================
//  Launchers
// ============================================================================

static void LaunchSimple(const wchar_t* exe) {
    HINSTANCE r = ShellExecuteW(NULL, L"open", exe, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32)
        LogMsg(L"Failed to launch '%ls' (ShellExecute code %Id).", exe, (INT_PTR)r);
}

static void SendCtrlKey(BYTE vk) {
    INPUT in[4] = {0};
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_CONTROL; in[0].ki.dwExtraInfo = MARK_INJECTED;
    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = vk;          in[1].ki.dwExtraInfo = MARK_INJECTED;
    in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = vk;          in[2].ki.dwFlags = KEYEVENTF_KEYUP; in[2].ki.dwExtraInfo = MARK_INJECTED;
    in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_CONTROL;  in[3].ki.dwFlags = KEYEVENTF_KEYUP; in[3].ki.dwExtraInfo = MARK_INJECTED;
    SendInput(4, in, sizeof(INPUT));
}

static void LaunchOrActivateBrave(void) {
    static const wchar_t* candidates[3];
    wchar_t localAppData[MAX_PATH] = L"";
    wchar_t bravePath3[MAX_PATH]   = L"";

    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        swprintf(bravePath3, MAX_PATH,
                 L"%ls\\BraveSoftware\\Brave-Browser\\Application\\brave.exe", localAppData);

    candidates[0] = L"C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe";
    candidates[1] = L"C:\\Program Files (x86)\\BraveSoftware\\Brave-Browser\\Application\\brave.exe";
    candidates[2] = bravePath3[0] ? bravePath3 : L"";

    const wchar_t* found = NULL;
    for (int i = 0; i < 3; i++) {
        if (candidates[i][0] && FileExistsW(candidates[i])) { found = candidates[i]; break; }
    }

    if (!found) {
        MessageBoxW(NULL, L"Could not find Brave. Please install it.", L"wm",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    HWND existing = FindMainWindowByProcessName(L"brave.exe");
    if (existing) {
        ActivateForFocusFollow(existing);

        // Bug #9 fix: the old 2-second spin-wait (40 × 50 ms) sent Ctrl+N
        // into whatever window happened to be in the foreground at the moment
        // the poll happened to succeed — on a loaded machine Brave might not
        // have come forward yet.  We now use a short but generous fixed wait
        // (300 ms) which is more than enough for a window that is already
        // open, then confirm ownership before sending the keystroke.
        Sleep(300);
        if (GetForegroundWindow() == existing)
            SendCtrlKey('N');
        else
            LogMsg(L"LaunchOrActivateBrave: Brave did not become foreground in time; Ctrl+N skipped.");
        return;
    }

    ShellExecuteW(NULL, L"open", found, NULL, NULL, SW_SHOWNORMAL);
}

// ============================================================================
//  Shell hook: spawn new windows centered under the mouse cursor
// ============================================================================

void HandleNewWindow(HWND h) {
    LONG_PTR exStyle = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) || (exStyle & WS_EX_NOACTIVATE)) return;

    wchar_t proc[MAX_PATH];
    GetWindowProcessNameSafe(h, proc, MAX_PATH);
    if (ShouldIgnoreNewWindowProcess(proc)) return;

    wchar_t cls[64];
    GetWindowClassSafe(h, cls, 64);
    if (IsShellClass(cls) || IsPopupClass(cls)) return;

    if (IsZoomed(h) || IsIconic(h)) return;

    // Bug #10 fix: Explorer (CabinetWClass) sometimes reports a pre-layout
    // position during the HSHELL_WINDOWCREATED notification.  The original
    // code used a blind Sleep(30) which was too short on a loaded machine.
    // We now retry GetWindowRect up to ~300 ms, stopping as soon as the
    // window appears at a plausible on-screen location.
    if (StrEqI(cls, L"CabinetWClass")) {
        for (int attempt = 0; attempt < 10; attempt++) {
            Sleep(30);
            RECT probe;
            if (GetWindowRect(h, &probe) &&
                probe.left > -30000 && probe.top > -30000 &&
                (probe.right - probe.left) >= 100 &&
                (probe.bottom - probe.top) >= 100)
                break; // position looks real
        }
    }

    RECT wr;
    if (!GetWindowRect(h, &wr)) return;
    LONG winW = wr.right  - wr.left;
    LONG winH = wr.bottom - wr.top;
    if (wr.left < -30000 || wr.top < -30000) return;
    if (winW < 100 || winH < 100) return;

    POINT pt;
    GetCursorPos(&pt);

    LONG targetX = pt.x - winW / 2;
    LONG targetY = pt.y - winH / 2;

    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    GetMonitorInfoW(mon, &mi);

    targetX = max(mi.rcWork.left,  min(targetX, mi.rcWork.right  - winW));
    targetY = max(mi.rcWork.top,   min(targetY, mi.rcWork.bottom - winH));

    MoveWindow(h, targetX, targetY, winW, winH, TRUE);
    SyncWindowLayering(h);
}

// ============================================================================
//  Hooks
// ============================================================================

typedef struct { WORD vk; ActionId action; } HotkeyEntry;

// Plain Alt+<key> combos (Ctrl must be up). All block the default OS handling.
static const HotkeyEntry kAltHotkeys[] = {
    { 'S', ACT_LAUNCH_FIREFOX }, { 'X', ACT_LAUNCH_EXPLORER },
    { 'N', ACT_LAUNCH_NOTEPAD }, { VK_RETURN, ACT_LAUNCH_TERMINAL },
    { VK_SPACE, ACT_LAUNCH_BRAVE },
    { 'Z', ACT_CLOSE_WIN }, { 'C', ACT_MIN_WIN },
    { 'Q', ACT_SNAP_LEFT }, { 'E', ACT_SNAP_RIGHT },
    { 'W', ACT_TOGGLE_MAX }, { 'V', ACT_TOGGLE_FULLSCREEN },
    { 'T', ACT_TOGGLE_TOPMOST },
    { VK_ESCAPE, ACT_ESCAPE_FULLSCREEN }, { 'I', ACT_DEBUG_INFO },
    { '1', ACT_DESK_1 }, { '2', ACT_DESK_2 }, { '3', ACT_DESK_3 }, { '4', ACT_DESK_4_MOVE },
    { 'A', ACT_MOVE_DESK_A }, { 'D', ACT_MOVE_DESK_D }, { 'F', ACT_MOVE_DESK_F },
};
#define N_ALT_HOTKEYS (sizeof(kAltHotkeys) / sizeof(kAltHotkeys[0]))

LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);

    KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lParam;

    if (k->dwExtraInfo == MARK_INJECTED)
        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);

    BOOL isDown = (wParam == WM_KEYDOWN  || wParam == WM_SYSKEYDOWN);
    BOOL isUp   = (wParam == WM_KEYUP    || wParam == WM_SYSKEYUP);
    if (!isDown && !isUp) return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);

    static BOOL keyDownState[256] = {0};
    WORD vk = (WORD)k->vkCode;

    if (isUp) {
        if (vk < 256) keyDownState[vk] = FALSE;
        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
    }

    BOOL repeat   = (vk < 256) ? keyDownState[vk] : FALSE;
    if (vk < 256) keyDownState[vk] = TRUE;

    BOOL altDown  = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
    BOOL ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

    // Track Alt+Tab for the focus-follow cooldown, but ALWAYS pass it through —
    // this must never block the OS's own Alt-Tab switcher.
    if (altDown && vk == VK_TAB) {
        g_lastAltTabTick = GetTickCount();
        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
    }

    if (repeat) {
        // Don't re-fire actions on key-repeat; app launches especially should
        // happen once per physical press, not N times while held.
        if (altDown && !ctrlDown) {
            for (unsigned i = 0; i < N_ALT_HOTKEYS; i++)
                if (kAltHotkeys[i].vk == vk) return 1; // swallow repeats of our combos
        }
        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
    }

    if (altDown && !ctrlDown) {
        for (unsigned i = 0; i < N_ALT_HOTKEYS; i++) {
            if (kAltHotkeys[i].vk == vk) {
                PostMessageW(g_hiddenWnd, WM_APP_HOTKEY_ACTION, (WPARAM)kAltHotkeys[i].action, 0);
                return 1; // block default handling (system menu, etc.)
            }
        }
    }

    if (ctrlDown && !altDown && (vk == VK_SPACE || vk == 'F')) {
        HWND fg = GetForegroundWindow();
        wchar_t proc[MAX_PATH];
        GetWindowProcessNameSafe(fg, proc, MAX_PATH);
        if (StrEqI(proc, L"firefox.exe") || StrEqI(proc, L"brave.exe")) {
            ActionId act = (vk == VK_SPACE) ? ACT_FF_NEWTAB : ACT_FF_CLOSETAB;
            PostMessageW(g_hiddenWnd, WM_APP_HOTKEY_ACTION, (WPARAM)act, 0);
            return 1;
        }
    }

    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

// ============================================================================
//  Action dispatch (runs on the main thread, off the hook callbacks)
// ============================================================================

void DispatchAction(ActionId act) {
    switch (act) {
        case ACT_LAUNCH_FIREFOX:  LaunchSimple(L"firefox.exe");  break;
        case ACT_LAUNCH_EXPLORER: LaunchSimple(L"explorer.exe"); break;
        case ACT_LAUNCH_NOTEPAD:  LaunchSimple(L"notepad.exe");  break;
        case ACT_LAUNCH_TERMINAL: LaunchSimple(L"wt.exe");       break;
        case ACT_LAUNCH_BRAVE:    LaunchOrActivateBrave();        break;

        case ACT_CLOSE_WIN: CloseActiveWindow();   break;
        case ACT_MIN_WIN:   MinimizeActiveWindow(); break;
        case ACT_SNAP_LEFT:  if (IsRealWindow(GetForegroundWindow())) SnapTo(GetForegroundWindow(), TRUE);  break;
        case ACT_SNAP_RIGHT: if (IsRealWindow(GetForegroundWindow())) SnapTo(GetForegroundWindow(), FALSE); break;

        case ACT_TOGGLE_MAX:        ToggleMaximize();       break;
        case ACT_TOGGLE_FULLSCREEN: ToggleFakeFullscreen(); break;
        case ACT_TOGGLE_TOPMOST:    ToggleTopmostRule();    break;
        case ACT_ESCAPE_FULLSCREEN: EscapeFakeFullscreen(); break;
        case ACT_DEBUG_INFO:        ShowDebugInfo();         break;

        case ACT_DESK_1:      GoToDesktop(0);              break;
        case ACT_DESK_2:      GoToDesktop(1);              break;
        case ACT_DESK_3:      GoToDesktop(2);              break;
        case ACT_DESK_4_MOVE: MoveWindowToDesktop(3, TRUE); break;

        case ACT_MOVE_DESK_A: MoveWindowToDesktop(0, FALSE); break;
        case ACT_MOVE_DESK_D: MoveWindowToDesktop(1, FALSE); break;
        case ACT_MOVE_DESK_F: MoveWindowToDesktop(2, FALSE); break;

        case ACT_FF_NEWTAB:   SendCtrlKey('T'); break;
        case ACT_FF_CLOSETAB: SendCtrlKey('W'); break;

        default: break;
    }
}
