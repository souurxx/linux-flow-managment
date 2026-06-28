// ============================================================================
//  main.c — wm: native Win32 window manager (C rewrite of master_51.ahk)
//
//  Architecture notes (the "why" behind the hardening choices):
//
//   * Focus-follows-mouse and "did the foreground window move/resize" are
//     handled by a low-level mouse hook (WH_MOUSE_LL) and SetWinEventHook
//     respectively, NOT by polling timers. The OS only wakes us up when
//     something actually happens, so idle CPU usage is effectively zero.
//
//   * Hook callbacks (mouse hook, keyboard hook, WinEvent hook) do the
//     MINIMUM possible work and then PostMessage a custom WM_APP_* message
//     to the hidden window, where the real logic runs on the normal message
//     loop. Low-level hooks have a timeout (~ a few seconds, configurable
//     via LowLevelHooksTimeout) after which Windows silently unhooks a slow
//     callback — doing real Win32 work (WinGetClass-equivalent calls,
//     SetWindowPos, etc.) directly inside the hook risks exactly that.
//
//   * Window state (saved position/style for restore) is stored in a hash
//     table keyed by HWND (window_state.c). Every entry also stores the
//     window's class name + process name at save time, and any lookup that
//     finds a mismatch unlinks the stale entry on the spot — see the
//     comment on FindVerifiedNode() in window_state.c.
//
//   * Single-instance handoff uses real mutex ownership (WaitForSingleObject)
//     rather than polling FindWindow, so two near-simultaneous launches
//     can't both fall through and run concurrently — see the comment on
//     EnsureSingleInstance() below.
//
//   * Failures are logged to wm.log next to the executable instead of being
//     silently swallowed by empty try/catch-equivalents.
//
//   * A tray icon gives you a real way to exit / view the log without
//     Task Manager.
//
//  Source layout:
//      wm_common.h    — shared types, message/timer IDs, cross-file decls
//      window_state.c — HWND -> saved geometry/style hash table
//      util.c         — window identity / ignore-list / monitor helpers
//      focus.c        — focus-follows-mouse hook + base-layer z-order sync
//      desktop.c      — VirtualDesktopAccessor + snap-left/right
//      hotkeys.c      — keyboard hook, action dispatch, action handlers
//      tray.c         — tray icon + RoundedTB auto-hide
//      main.c         — entry point, message loop, config, logging (this file)
//
//  Build (MinGW-w64, from an x64 Native Tools / MSYS2 shell):
//      gcc -O2 -municode -mwindows *.c -o wm.exe ^
//          -luser32 -lgdi32 -ldwmapi -ladvapi32 -lshell32 -lole32
//
//  Build (MSVC, x64 Native Tools Command Prompt):
//      cl /O2 /DUNICODE /D_UNICODE *.c user32.lib gdi32.lib dwmapi.lib ^
//         advapi32.lib shell32.lib ole32.lib /link /SUBSYSTEM:WINDOWS
// ============================================================================

#include "wm_common.h"

// ── App identity ────────────────────────────────────────────────────────
static const wchar_t* WND_CLASS_NAME = L"WM_Hidden_3F2C9A1B";
static const wchar_t* MUTEX_NAME     = L"Local\\WM_SingleInstance_3F2C9A1B";

// ── Globals ──────────────────────────────────────────────────────────────
HWND    g_hiddenWnd         = NULL;
wchar_t g_exeDir[MAX_PATH]  = L"";

static HWINEVENTHOOK g_winEventHook   = NULL;
static HWINEVENTHOOK g_winEventHookFg = NULL;
static UINT          g_uShellHookMsg  = 0;

static wchar_t g_logPath[MAX_PATH] = L"";
static wchar_t g_iniPath[MAX_PATH] = L"";

static BOOL g_focusFollowsMouseEnabled = TRUE;
static BOOL g_hideRoundedTBEnabled     = TRUE;

wchar_t g_ignoreProcesses[32][MAX_PATH];
int     g_ignoreProcessesCount = 0;

wchar_t g_ignoreNewWindowProcesses[32][MAX_PATH];
int     g_ignoreNewWindowProcessesCount = 0;

wchar_t g_ignoreClasses[32][64];
int     g_ignoreClassesCount = 0;

// ── Forward decls ───────────────────────────────────────────────────────
static void LoadConfiguration(void);

// ============================================================================
//  Logging
// ============================================================================

void LogMsg(const wchar_t* fmt, ...) {
    FILE* f = _wfopen(g_logPath, L"a,ccs=UTF-8");
    if (!f) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(f, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    va_list args;
    va_start(args, fmt);
    vfwprintf(f, fmt, args);
    va_end(args);

    fwprintf(f, L"\n");
    fclose(f);
}

static void RotateLogIfHuge(void) {
    HANDLE h = CreateFileW(g_logPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER size;
    GetFileSizeEx(h, &size);
    CloseHandle(h);

    if (size.QuadPart > 2 * 1024 * 1024) {
        wchar_t oldPath[MAX_PATH];
        swprintf(oldPath, MAX_PATH, L"%ls.old", g_logPath);
        DeleteFileW(oldPath);
        MoveFileW(g_logPath, oldPath);
    }
}

static void WriteDefaultIni(void) {
    FILE* f = _wfopen(g_iniPath, L"w,ccs=UTF-8");
    if (!f) return;

    fwprintf(f, L"[Settings]\n");
    fwprintf(f, L"FocusFollowsMouse=1\n");
    fwprintf(f, L"HideRoundedTB=1\n\n");

    fwprintf(f, L"[IgnoreProcesses]\n");
    fwprintf(f, L"proc1=AltSnap.exe\n");
    fwprintf(f, L"proc2=StartMenuExperienceHost.exe\n");
    fwprintf(f, L"proc3=SearchHost.exe\n");
    fwprintf(f, L"proc4=ShellExperienceHost.exe\n");
    fwprintf(f, L"proc5=TextInputHost.exe\n\n");

    fwprintf(f, L"[IgnoreNewWindowProcesses]\n");
    fwprintf(f, L"proc1=zebar.exe\n");
    fwprintf(f, L"proc2=glazewm.exe\n");
    fwprintf(f, L"proc3=Telegram.exe\n");
    fwprintf(f, L"proc4=steam.exe\n");
    fwprintf(f, L"proc5=steamwebhelper.exe\n");
    fwprintf(f, L"proc6=wallpaper64.exe\n");
    fwprintf(f, L"proc7=obs64.exe\n");
    fwprintf(f, L"proc8=Discord.exe\n\n");

    fwprintf(f, L"[IgnoreClasses]\n");
    fwprintf(f, L"class1=WorkerW\n");
    fwprintf(f, L"class2=Progman\n");
    fwprintf(f, L"class3=Shell_TrayWnd\n");
    fwprintf(f, L"class4=Shell_SecondaryTrayWnd\n");

    fclose(f);
}

// Bug #14 fix: GetPrivateProfileSectionW includes the full "key=value" string
// (including any trailing inline comment like "; description") verbatim.
// Strip everything from the first semicolon onward, then trim trailing spaces,
// so user-added comments in the INI file don't become part of a process/class
// name that will never match anything.
static void StripInlineComment(wchar_t* s) {
    wchar_t* semi = wcschr(s, L';');
    if (semi) *semi = L'\0';
    // Trim trailing whitespace left by the strip.
    int len = (int)wcslen(s);
    while (len > 0 && (s[len - 1] == L' ' || s[len - 1] == L'\t')) {
        s[--len] = L'\0';
    }
}

static void LoadConfiguration(void) {
    DWORD attr = GetFileAttributesW(g_iniPath);
    BOOL exists = (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
    if (!exists)
        WriteDefaultIni();

    g_focusFollowsMouseEnabled = GetPrivateProfileIntW(L"Settings", L"FocusFollowsMouse", 1, g_iniPath) != 0;
    g_hideRoundedTBEnabled     = GetPrivateProfileIntW(L"Settings", L"HideRoundedTB",     1, g_iniPath) != 0;

    wchar_t buf[4096];

    // ── [IgnoreProcesses] ────────────────────────────────────────────────
    g_ignoreProcessesCount = 0;
    DWORD len = GetPrivateProfileSectionW(L"IgnoreProcesses", buf, 4096, g_iniPath);
    if (len > 0 && len < 4096 - 2) {
        wchar_t* p = buf;
        while (*p && g_ignoreProcessesCount < 32) {
            wchar_t* eq = wcschr(p, L'=');
            if (eq) {
                wchar_t* val = eq + 1;
                StripInlineComment(val);
                if (wcslen(val) > 0 && wcslen(val) < MAX_PATH) {
                    wcsncpy_s(g_ignoreProcesses[g_ignoreProcessesCount], MAX_PATH, val, _TRUNCATE);
                    g_ignoreProcessesCount++;
                }
            }
            p += wcslen(p) + 1;
        }
    } else {
        static const wchar_t* defaults[] = {
            L"AltSnap.exe", L"StartMenuExperienceHost.exe", L"SearchHost.exe",
            L"ShellExperienceHost.exe", L"TextInputHost.exe"
        };
        for (int i = 0; i < 5; i++)
            wcsncpy_s(g_ignoreProcesses[i], MAX_PATH, defaults[i], _TRUNCATE);
        g_ignoreProcessesCount = 5;
    }

    // ── [IgnoreNewWindowProcesses] ───────────────────────────────────────
    g_ignoreNewWindowProcessesCount = 0;
    len = GetPrivateProfileSectionW(L"IgnoreNewWindowProcesses", buf, 4096, g_iniPath);
    if (len > 0 && len < 4096 - 2) {
        wchar_t* p = buf;
        while (*p && g_ignoreNewWindowProcessesCount < 32) {
            wchar_t* eq = wcschr(p, L'=');
            if (eq) {
                wchar_t* val = eq + 1;
                StripInlineComment(val);
                if (wcslen(val) > 0 && wcslen(val) < MAX_PATH) {
                    wcsncpy_s(g_ignoreNewWindowProcesses[g_ignoreNewWindowProcessesCount], MAX_PATH, val, _TRUNCATE);
                    g_ignoreNewWindowProcessesCount++;
                }
            }
            p += wcslen(p) + 1;
        }
    } else {
        static const wchar_t* defaults[] = {
            L"zebar.exe", L"glazewm.exe", L"Telegram.exe", L"steam.exe",
            L"steamwebhelper.exe", L"wallpaper64.exe", L"obs64.exe", L"Discord.exe"
        };
        for (int i = 0; i < 8; i++)
            wcsncpy_s(g_ignoreNewWindowProcesses[i], MAX_PATH, defaults[i], _TRUNCATE);
        g_ignoreNewWindowProcessesCount = 8;
    }

    // ── [IgnoreClasses] ──────────────────────────────────────────────────
    g_ignoreClassesCount = 0;
    len = GetPrivateProfileSectionW(L"IgnoreClasses", buf, 4096, g_iniPath);
    if (len > 0 && len < 4096 - 2) {
        wchar_t* p = buf;
        while (*p && g_ignoreClassesCount < 32) {
            wchar_t* eq = wcschr(p, L'=');
            if (eq) {
                wchar_t* val = eq + 1;
                StripInlineComment(val);
                if (wcslen(val) > 0 && wcslen(val) < 64) {
                    wcsncpy_s(g_ignoreClasses[g_ignoreClassesCount], 64, val, _TRUNCATE);
                    g_ignoreClassesCount++;
                }
            }
            p += wcslen(p) + 1;
        }
    } else {
        static const wchar_t* defaults[] = {
            L"WorkerW", L"Progman", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"
        };
        for (int i = 0; i < 4; i++)
            wcsncpy_s(g_ignoreClasses[i], 64, defaults[i], _TRUNCATE);
        g_ignoreClassesCount = 4;
    }
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* exceptionInfo) {
    (void)exceptionInfo;
    LogMsg(L"Fatal error occurred. Cleaning up hooks before exit.");
    if (g_mouseHook)      { UnhookWindowsHookEx(g_mouseHook);   g_mouseHook   = NULL; }
    if (g_keyboardHook)   { UnhookWindowsHookEx(g_keyboardHook); g_keyboardHook = NULL; }
    if (g_winEventHook)   { UnhookWinEvent(g_winEventHook);      g_winEventHook  = NULL; }
    if (g_winEventHookFg) { UnhookWinEvent(g_winEventHookFg);    g_winEventHookFg = NULL; }
    RemoveTrayIcon(g_hiddenWnd);
    return EXCEPTION_EXECUTE_HANDLER;
}

// ============================================================================
//  Elevation
// ============================================================================

static BOOL IsElevated(void) {
    BOOL elevated = FALSE;
    HANDLE token  = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION te;
        DWORD sz = sizeof(te);
        if (GetTokenInformation(token, TokenElevation, &te, sizeof(te), &sz))
            elevated = te.TokenIsElevated;
        CloseHandle(token);
    }
    return elevated;
}

static void ElevateAndRestartOrExit(void) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb       = L"runas";
    sei.lpFile       = exePath;
    sei.lpParameters = L"/restart";
    sei.nShow        = SW_SHOWNORMAL;
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;

    if (!ShellExecuteExW(&sei)) {
        MessageBoxW(NULL,
                    L"This app requires administrator privileges and elevation was declined.",
                    L"wm", MB_OK | MB_ICONWARNING);
    }
    ExitProcess(0);
}

// ============================================================================
//  Single instance
// ============================================================================

static HANDLE EnsureSingleInstance(void) {
    HANDLE mtx = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (!mtx) {
        // CreateMutex failed entirely — we cannot guarantee single-instance.
        // Log and exit rather than risk two copies running concurrently.
        // (Bug #7 fix: the old code continued with a NULL mutex.)
        LogMsg(L"CreateMutexW failed (error %lu); cannot enforce single instance. Exiting.", GetLastError());
        ExitProcess(1);
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance is running. Ask it to exit, then wait for it to
        // release the mutex before we take over.
        HWND old = FindWindowW(WND_CLASS_NAME, NULL);
        if (old) PostMessageW(old, WM_CLOSE, 0, 0);

        DWORD wait = WaitForSingleObject(mtx, 5000);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
            LogMsg(L"Single-instance handoff timed out; exiting.");
            CloseHandle(mtx);
            ExitProcess(0);
        }
    }
    return mtx;
}

// ============================================================================
//  Window procedure
// ============================================================================

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Bug #15 fix: guard the shell-hook check against g_uShellHookMsg == 0.
    // RegisterWindowMessageW returns 0 on failure; if we compared msg == 0
    // unconditionally we'd catch WM_NULL and misroute it as a shell event.
    if (g_uShellHookMsg && msg == g_uShellHookMsg) {
        if (wParam == HSHELL_WINDOWCREATED) {
            HWND newWin = (HWND)lParam;
            if (IsWindow(newWin))
                HandleNewWindow(newWin);
        }
        return 0;
    }

    switch (msg) {

        case WM_APP_MOUSEMOVE:
            FocusFollowsMouse();
            return 0;

        case WM_APP_FOREGROUND_CHANGED: {
            HWND h = (HWND)wParam;
            if (!ShouldIgnoreFocusWindow(h))
                SyncWindowLayering(h);
            return 0;
        }

        case WM_APP_LAYERING_RECHECK: {
            HWND h = (HWND)wParam;
            if (h && IsWindow(h) && !ShouldIgnoreFocusWindow(h))
                SyncWindowLayering(h);
            return 0;
        }

        case WM_APP_HOTKEY_ACTION:
            DispatchAction((ActionId)wParam);
            return 0;

        case WM_APP_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
                ShowTrayMenu(hwnd);
            return 0;

        // Bug #1 fix: handle the menu-popup message that WinEventProc posts
        // when EVENT_SYSTEM_MENUPOPUPSTART fires.  Force the menu window to
        // the topmost band so it renders above any of our promoted floating
        // windows.  This was previously missing entirely — the message was
        // posted but never consumed, so menus could appear underneath topmost
        // app windows.
        case WM_APP_MENU_POPUP: {
            HWND menuWnd = (HWND)wParam;
            if (menuWnd && IsWindow(menuWnd))
                SetWindowPos(menuWnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == ID_TRAY_VIEWLOG) {
                ShellExecuteW(NULL, L"open", L"notepad.exe", g_logPath, NULL, SW_SHOWNORMAL);
            }
            return 0;

        case WM_DESTROY:
            RemoveTrayIcon(hwnd);
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    if (msg == WM_TIMER) {
        switch (wParam) {
            case TIMER_ROUNDEDTB_HIDE:    TryHideRoundedTB(hwnd); break;
            case TIMER_STATE_CLEANUP:     CleanupStaleNodes();     break;
            case TIMER_LAYERING_SAFETYNET: {
                HWND fg = GetForegroundWindow();
                if (fg && !ShouldIgnoreFocusWindow(fg)) SyncWindowLayering(fg);
                break;
            }
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
//  Entry point
// ============================================================================

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrevInst, PWSTR cmdLine, int nCmdShow) {
    (void)hPrevInst; (void)nCmdShow;

    SetUnhandledExceptionFilter(CrashHandler);

    GetModuleFileNameW(NULL, g_exeDir, MAX_PATH);
    wchar_t* slash = wcsrchr(g_exeDir, L'\\');
    if (slash) *slash = 0;
    swprintf(g_logPath, MAX_PATH, L"%ls\\wm.log",  g_exeDir);
    swprintf(g_iniPath, MAX_PATH, L"%ls\\wm.ini",  g_exeDir);
    RotateLogIfHuge();

    LoadConfiguration();

    LogMsg(L"=== Starting up ===");

    BOOL restarting = (cmdLine && wcsstr(cmdLine, L"/restart") != NULL);
    if (!IsElevated() && !restarting) {
        LogMsg(L"Not elevated, requesting UAC elevation.");
        ElevateAndRestartOrExit();
        return 0;
    }

    HANDLE mutex = EnsureSingleInstance(); // exits on failure

    LoadVirtualDesktopAccessor();

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = WND_CLASS_NAME;
    RegisterClassExW(&wc);

    g_hiddenWnd = CreateWindowExW(0, WND_CLASS_NAME, L"wm", 0,
                                  0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);
    if (!g_hiddenWnd) {
        LogMsg(L"Failed to create hidden window. Exiting.");
        return 1;
    }

    g_uShellHookMsg = RegisterWindowMessageW(L"SHELLHOOK");
    if (!g_uShellHookMsg)
        LogMsg(L"RegisterWindowMessageW(SHELLHOOK) failed — new-window centering disabled.");
    else
        RegisterShellHookWindow(g_hiddenWnd);

    if (g_focusFollowsMouseEnabled) {
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, hInst, 0);
        if (!g_mouseHook) LogMsg(L"Failed to install mouse hook (focus-follows-mouse disabled).");
    }

    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, hInst, 0);
    if (!g_keyboardHook) LogMsg(L"Failed to install keyboard hook (hotkeys disabled).");

    // Bug #2 fix: the WinEvent subscription range must include
    // EVENT_SYSTEM_MENUPOPUPSTART (0x0006) so WinEventProc actually receives
    // menu-popup events.  The old range started at EVENT_SYSTEM_MOVESIZESTART
    // (0x000A) which is *above* 0x0006, so menu events were never delivered.
    //
    // New range: EVENT_SYSTEM_MENUPOPUPSTART (0x0006) – EVENT_SYSTEM_MINIMIZEEND (0x0017).
    // This is a superset of the old range and covers all events we handle:
    //   0x0006  EVENT_SYSTEM_MENUPOPUPSTART
    //   0x000B  EVENT_SYSTEM_MOVESIZEEND
    //   0x0016  EVENT_SYSTEM_MINIMIZESTART
    //   0x0017  EVENT_SYSTEM_MINIMIZEEND
    // (EVENT_SYSTEM_MOVESIZESTART 0x000A is inside this range too; it arrives
    //  at WinEventProc but hits the default case and is ignored — no cost.)
    g_winEventHook = SetWinEventHook(
        EVENT_SYSTEM_MENUPOPUPSTART, EVENT_SYSTEM_MINIMIZEEND,
        NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

    g_winEventHookFg = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

    if (!g_winEventHook)
        LogMsg(L"Failed to install WinEvent hook (move/size/minimize/menu layering sync degraded to safety-net timer only).");
    if (!g_winEventHookFg)
        LogMsg(L"Failed to install foreground WinEvent hook (foreground-change layering sync degraded to safety-net timer only).");

    AddTrayIcon(g_hiddenWnd);

    if (g_hideRoundedTBEnabled)
        SetTimer(g_hiddenWnd, TIMER_ROUNDEDTB_HIDE, 500, NULL);

    SetTimer(g_hiddenWnd, TIMER_STATE_CLEANUP,      5000, NULL);
    SetTimer(g_hiddenWnd, TIMER_LAYERING_SAFETYNET, 5000, NULL); // fallback; events do the real work

    LogMsg(L"Initialization complete. VDA available: %ls", g_vdaAvailable ? L"yes" : L"no");

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    LogMsg(L"=== Shutting down ===");

    if (g_mouseHook)      UnhookWindowsHookEx(g_mouseHook);
    if (g_keyboardHook)   UnhookWindowsHookEx(g_keyboardHook);
    if (g_winEventHook)   UnhookWinEvent(g_winEventHook);
    if (g_winEventHookFg) UnhookWinEvent(g_winEventHookFg);
    if (g_vdaModule)      FreeLibrary(g_vdaModule);
    if (mutex)            { ReleaseMutex(mutex); CloseHandle(mutex); }

    FreeAllWindowState();

    return (int)msg.wParam;
}
