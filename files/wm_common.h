// ============================================================================
//  wm_common.h — shared types, message/timer IDs, and cross-file declarations
//  for the wm window manager.
//
//  See main.c for the overall architecture notes and build instructions.
//  Every .c file in this project includes this header and nothing else
//  besides its own private helpers, so this is the single place that
//  defines the contract between modules.
// ============================================================================

#ifndef WM_COMMON_H
#define WM_COMMON_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>

#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// ── Custom messages posted from hooks -> processed on the main message loop ─
#define WM_APP_MOUSEMOVE          (WM_APP + 1)
#define WM_APP_FOREGROUND_CHANGED (WM_APP + 2)
#define WM_APP_LAYERING_RECHECK   (WM_APP + 3)
#define WM_APP_HOTKEY_ACTION      (WM_APP + 4)
#define WM_APP_TRAYICON           (WM_APP + 5)
#define WM_APP_MENU_POPUP         (WM_APP + 6)

#define TRAY_ID                1001
#define ID_TRAY_EXIT            2001
#define ID_TRAY_VIEWLOG         2002

#define TIMER_ROUNDEDTB_HIDE     1
#define TIMER_STATE_CLEANUP      2
#define TIMER_LAYERING_SAFETYNET 3

#define ALTWSTATE_NORMAL    0
#define ALTWSTATE_MAXIMIZED 1
#define ALTWSTATE_FULLSCREEN 2

#define MARK_INJECTED ((ULONG_PTR)0xC0FFEE42)

// ── Window-state hash table node (window_state.c) ───────────────────────────
// The struct itself is shared (not just an opaque pointer) because the
// hotkey handlers in hotkeys.c read/write geometry and altState fields
// directly while saving/restoring window state, same as the original
// single-file design.
typedef struct WSNode {
    HWND   hwnd;
    BOOL   hasPos;          // saved geometry valid?
    LONG   x, y, w, h;
    BOOL   wasTopmost;
    LONG_PTR style, exStyle;
    int    altState;        // ALTWSTATE_*
    wchar_t className[64];
    wchar_t processName[MAX_PATH];
    struct WSNode* next;
} WSNode;

// ── Hotkey action IDs (hotkeys.c), also referenced by main.c's WndProc ─────
typedef enum {
    ACT_NONE = 0,
    ACT_LAUNCH_FIREFOX, ACT_LAUNCH_EXPLORER, ACT_LAUNCH_NOTEPAD, ACT_LAUNCH_TERMINAL,
    ACT_LAUNCH_BRAVE,
    ACT_CLOSE_WIN, ACT_MIN_WIN, ACT_SNAP_LEFT, ACT_SNAP_RIGHT,
    ACT_TOGGLE_MAX, ACT_TOGGLE_FULLSCREEN, ACT_ESCAPE_FULLSCREEN, ACT_DEBUG_INFO,
    ACT_DESK_1, ACT_DESK_2, ACT_DESK_3, ACT_DESK_4_MOVE,
    ACT_MOVE_DESK_A, ACT_MOVE_DESK_D, ACT_MOVE_DESK_F,
    ACT_FF_NEWTAB, ACT_FF_CLOSETAB,
    ACT_TOGGLE_TOPMOST
} ActionId;

// ============================================================================
//  Cross-file globals
//  (each is defined with real storage in exactly one .c file, noted below)
// ============================================================================

extern HWND  g_hiddenWnd;                    // defined in main.c
extern wchar_t g_exeDir[MAX_PATH];           // defined in main.c

extern wchar_t g_ignoreProcesses[32][MAX_PATH];          // defined in main.c
extern int     g_ignoreProcessesCount;                   // defined in main.c
extern wchar_t g_ignoreNewWindowProcesses[32][MAX_PATH]; // defined in main.c
extern int     g_ignoreNewWindowProcessesCount;           // defined in main.c
extern wchar_t g_ignoreClasses[32][64];                  // defined in main.c
extern int     g_ignoreClassesCount;                     // defined in main.c

extern HHOOK g_keyboardHook;   // defined in hotkeys.c
extern HHOOK g_mouseHook;      // defined in focus.c
extern DWORD g_lastAltTabTick; // defined in focus.c

extern HMODULE g_vdaModule;    // defined in desktop.c
extern BOOL    g_vdaAvailable; // defined in desktop.c

// ============================================================================
//  Cross-file function prototypes, grouped by the .c file that defines them
// ============================================================================

// -- main.c ------------------------------------------------------------------
void LogMsg(const wchar_t* fmt, ...);

// -- window_state.c ------------------------------------------------------------
WSNode* FindVerifiedNode(HWND h);
WSNode* GetOrCreateNode(HWND h);
void    CleanupStaleNodes(void);
void    ClearWindowState(HWND h, BOOL allowRestore);
void    RestoreWindowState(HWND h);
void    FreeAllWindowState(void); // releases the whole table; call once at shutdown

// -- util.c --------------------------------------------------------------------
void GetWindowClassSafe(HWND h, wchar_t* buf, int buflen);
void GetWindowProcessNameSafe(HWND h, wchar_t* buf, int buflen);
BOOL StrEqI(const wchar_t* a, const wchar_t* b);
BOOL IsShellClass(const wchar_t* c);
BOOL IsPopupClass(const wchar_t* c);
BOOL ShouldIgnoreNewWindowProcess(const wchar_t* p);
BOOL IsFirefoxWindow(HWND h);
BOOL ShouldIgnoreFocusWindow(HWND h);
BOOL CanMoveToDesktop(HWND h);
BOOL IsRealWindow(HWND h);
void GetMonitorWorkAreaForWindow(HWND h, RECT* out);
void GetMonitorBoundsForWindow(HWND h, RECT* out);
void HideTaskbarForWindow(HWND h);
void ShowTaskbarForWindow(HWND h);
void MoveWindowVisibleRect(HWND h, LONG l, LONG t, LONG r, LONG b);
BOOL RestoreWindowDirectlyToRect(HWND h, LONG l, LONG t, LONG r, LONG b);
BOOL FileExistsW(const wchar_t* path);
HWND FindMainWindowByProcessName(const wchar_t* exeName);

// -- focus.c ---------------------------------------------------------------------
void SyncWindowLayering(HWND h);
void ToggleTopmostRule(void);
void UnpinAllWindows(void);
void ActivateForFocusFollow(HWND h);
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
void FocusFollowsMouse(void);
void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                            LONG idObject, LONG idChild, DWORD idThread, DWORD msTime);

// -- desktop.c -------------------------------------------------------------------
void LoadVirtualDesktopAccessor(void);
void GoToDesktop(int n);
void MoveWindowToDesktop(int n, BOOL alsoGo);
void SnapTo(HWND h, BOOL left);

// -- hotkeys.c -------------------------------------------------------------------
void HandleNewWindow(HWND h);
LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
void DispatchAction(ActionId act);

// -- tray.c ----------------------------------------------------------------------
void AddTrayIcon(HWND hwnd);
void RemoveTrayIcon(HWND hwnd);
void ShowTrayMenu(HWND hwnd);
void TryHideRoundedTB(HWND hiddenWnd);

#endif // WM_COMMON_H
