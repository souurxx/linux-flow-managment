// ============================================================================
//  focus.c — focus-follows-mouse (WH_MOUSE_LL hook, no polling timer),
//  z-order sync, and the WinEvent hook that drives layering re-checks.
//
//  Z-ORDER DESIGN
//  ──────────────
//  wm manages two z-order concerns and nothing else:
//
//  2. FAKE-FULLSCREEN DEMOTION
//     Windows wm itself put into fake-fullscreen (ALTWSTATE_FULLSCREEN) are
//     kept in the normal (non-topmost) z-band via HWND_NOTOPMOST so they
//     don't cover system overlays (clock, notification flyouts, etc.).
//
//  3. CONTEXT MENUS
//     Context menus are non-topmost popup windows.  If a pinned topmost window
//     is on screen, a freshly opened menu can appear underneath it.
//     WM_APP_MENU_POPUP forces the menu topmost once; it's destroyed in
//     seconds so there's nothing to restore.
//
//  wm tracks which windows it pinned in g_pinnedSet so it can unpin them
//  cleanly on shutdown and when Alt+T is toggled off — no windows are left
//  stuck as HWND_TOPMOST after wm exits.
// ============================================================================

#include "wm_common.h"

HHOOK g_mouseHook      = NULL;
DWORD g_lastAltTabTick = 0;

// ============================================================================
//  Pinned-window tracking
//  We keep a small fixed array of HWNDs that wm itself promoted to topmost
//  via Alt+T.  On shutdown (or Alt+T-disable) we demote them all so nothing
//  is left stuck.  Max 64 pinned windows is more than enough in practice.
// ============================================================================

#define MAX_PINNED 64
static HWND g_pinned[MAX_PINNED];
static int  g_pinnedCount = 0;

static BOOL IsPinned(HWND h) {
    for (int i = 0; i < g_pinnedCount; i++)
        if (g_pinned[i] == h) return TRUE;
    return FALSE;
}

static void AddPinned(HWND h) {
    if (IsPinned(h)) return;
    if (g_pinnedCount < MAX_PINNED)
        g_pinned[g_pinnedCount++] = h;
}

static void RemovePinned(HWND h) {
    for (int i = 0; i < g_pinnedCount; i++) {
        if (g_pinned[i] == h) {
            g_pinned[i] = g_pinned[--g_pinnedCount];
            return;
        }
    }
}

// Called at shutdown — demote every window wm pinned so nothing is left stuck.
void UnpinAllWindows(void) {
    for (int i = 0; i < g_pinnedCount; i++) {
        if (IsWindow(g_pinned[i]))
            SetWindowPos(g_pinned[i], HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    g_pinnedCount = 0;
}

// ============================================================================
//  Z-order helpers
// ============================================================================

// SyncWindowLayering — called after wm changes a window's state (entering /
// leaving fake-fullscreen, foreground change, move/resize end).
// Only acts on windows wm is explicitly responsible for:
//   • Fake-fullscreen: demote to NOTOPMOST.
//   • Everything else: leave completely alone.
void SyncWindowLayering(HWND h) {
    if (!h || !IsWindow(h)) return;

    WSNode* n = FindVerifiedNode(h);
    if (n && n->altState == ALTWSTATE_FULLSCREEN) {
        SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    // Normal and maximized windows: do not touch. The OS manages their z-order.
}

// ToggleTopmostRule — Alt+T: per-window always-on-top pin toggle.
//
// Each press pins or unpins the foreground window:
//   • Not pinned → set HWND_TOPMOST, add to tracking list.
//   • Already pinned → set HWND_NOTOPMOST, remove from tracking list.
//
// wm tracks every window it pins so UnpinAllWindows() can clean them up
// on shutdown — no window is ever left stuck as HWND_TOPMOST after wm exits.
//
void ToggleTopmostRule(void) {
    HWND fg = GetForegroundWindow();
    if (!fg || !IsWindow(fg) || !IsRealWindow(fg)) return;

    if (IsPinned(fg)) {
        // Already pinned — unpin it.
        SetWindowPos(fg, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RemovePinned(fg);
        LogMsg(L"Unpinned hwnd 0x%p", fg);
    } else {
        // Not pinned — pin it.
        SetWindowPos(fg, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        AddPinned(fg);
        LogMsg(L"Pinned hwnd 0x%p as topmost", fg);
    }
}

// ============================================================================
//  ForceSetForegroundWindow
// ============================================================================

static void ForceSetForegroundWindow(HWND h) {
    if (SetForegroundWindow(h)) return;

    HWND curFg = GetForegroundWindow();
    if (!curFg || curFg == h) return;

    DWORD curThread    = GetWindowThreadProcessId(curFg, NULL);
    DWORD targetThread = GetWindowThreadProcessId(h, NULL);
    DWORD myThread     = GetCurrentThreadId();

    BOOL attachedCur    = FALSE;
    BOOL attachedTarget = FALSE;

    if (curThread && curThread != myThread)
        attachedCur = AttachThreadInput(myThread, curThread, TRUE);

    if (targetThread && targetThread != myThread && targetThread != curThread)
        attachedTarget = AttachThreadInput(myThread, targetThread, TRUE);

    if (!SetForegroundWindow(h)) {
        // Fallback: synthesise an Alt press to bypass the foreground lock.
        // MARK_INJECTED ensures our keyboard hook ignores these synthetic
        // events and doesn't fire hotkey actions on them.
        keybd_event(VK_MENU, 0, 0,               MARK_INJECTED);
        SetForegroundWindow(h);
        keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, MARK_INJECTED);
    }
    BringWindowToTop(h);

    if (attachedTarget)
        AttachThreadInput(myThread, targetThread, FALSE);
    if (attachedCur)
        AttachThreadInput(myThread, curThread, FALSE);
}

void ActivateForFocusFollow(HWND h) {
    SyncWindowLayering(h);
    ForceSetForegroundWindow(h);
}

// ============================================================================
//  Focus-follows-mouse — WH_MOUSE_LL hook (no polling timer)
// ============================================================================

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_MOUSEMOVE) {
        static LONG lastHookX = -999999, lastHookY = -999999;
        const MSLLHOOKSTRUCT* m = (const MSLLHOOKSTRUCT*)lParam;
        if (m->pt.x != lastHookX || m->pt.y != lastHookY) {
            lastHookX = m->pt.x;
            lastHookY = m->pt.y;
            PostMessageW(g_hiddenWnd, WM_APP_MOUSEMOVE, 0, 0);
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

void FocusFollowsMouse(void) {
    static HWND lastHwnd = NULL;
    static LONG lastX = -999999, lastY = -999999;

    if (GetTickCount() - g_lastAltTabTick < 400) return;

    POINT pt;
    GetCursorPos(&pt);

    if (pt.x == lastX && pt.y == lastY) return;
    lastX = pt.x;
    lastY = pt.y;

    HWND h = WindowFromPoint(pt);
    if (!h) return;

    h = GetAncestor(h, GA_ROOT);
    if (!h || h == lastHwnd) return;
    if (ShouldIgnoreFocusWindow(h)) return;

    HWND active = GetForegroundWindow();
    if (active && h == active) { lastHwnd = h; return; }

    if (!IsWindow(h)) return;

    lastHwnd = h;
    ActivateForFocusFollow(h);
}

// ============================================================================
//  WinEvent hook
// ============================================================================

void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                            LONG idObject, LONG idChild, DWORD idThread, DWORD msTime) {
    (void)hook; (void)idThread; (void)msTime;
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd) return;

    switch (event) {
        case EVENT_SYSTEM_FOREGROUND:
            PostMessageW(g_hiddenWnd, WM_APP_FOREGROUND_CHANGED, (WPARAM)hwnd, 0);
            break;

        case EVENT_SYSTEM_MOVESIZEEND:
        case EVENT_SYSTEM_MINIMIZESTART:
        case EVENT_SYSTEM_MINIMIZEEND:
            PostMessageW(g_hiddenWnd, WM_APP_LAYERING_RECHECK, (WPARAM)hwnd, 0);
            break;

        // Force context/system menus topmost so they don't hide behind a
        // fake-fullscreen window that wm demoted to NOTOPMOST, or behind a
        // window the user pinned via Alt+T.
        case EVENT_SYSTEM_MENUPOPUPSTART:
            PostMessageW(g_hiddenWnd, WM_APP_MENU_POPUP, (WPARAM)hwnd, 0);
            break;

        default:
            break;
    }
}
