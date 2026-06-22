// ============================================================================
//  focus.c — focus-follows-mouse (WH_MOUSE_LL hook, no polling timer),
//  base-layer / always-on-top z-order sync, and the WinEvent hook that
//  drives layering re-checks on foreground/move/minimize changes.
// ============================================================================

#include "wm_common.h"

HHOOK g_mouseHook = NULL;
DWORD g_lastAltTabTick = 0;

// ============================================================================
//  Base-layer / always-on-top sync (kept maximized & fake-fullscreen windows
//  in the OS "normal" z-order band so floating windows can't be buried)
// ============================================================================

// A window only belongs in the "base layer" (non-topmost) band if we
// actually know why it's full-screen: the OS maximized it, or WE put it
// into fake fullscreen via Alt+V. We deliberately do NOT infer this from
// pixel size anymore — a third-party app's own screen-filling popup
// (Telegram's media viewer, FastStone's fullscreen view, a video player's
// overlay, etc.) is indistinguishable from "a maximized app you alt-tabbed
// away from" by size alone, and those apps usually set WS_EX_TOPMOST on
// such popups themselves on purpose. Demoting them was stripping a flag
// the app deliberately set, which both let it fall behind other topmost
// windows AND left it covering the whole monitor in real screen space —
// so WindowFromPoint kept resolving to it everywhere, trapping
// focus-follow-mouse and every hotkey on that one window even while
// hovering elsewhere. Only touch topmost status for windows whose
// fullscreen-ness we ourselves are responsible for.
static BOOL IsBaseLayerWindow(HWND h) {
    if (IsZoomed(h)) return TRUE;

    WSNode* n = FindVerifiedNode(h);
    if (n && n->altState == ALTWSTATE_FULLSCREEN) return TRUE;

    return FALSE;
}

void SyncWindowLayering(HWND h) {
    // Don't touch z-order while this window's thread has a menu open.
    // Native menu popups aren't WS_EX_TOPMOST — Windows places them above
    // their owner at show time — so reasserting HWND_TOPMOST on the owner
    // while the menu is still visible buries it behind the owner. Skipping
    // here is safe: the next event or safety-net timer fires once the menu
    // is closed and catches up.
    DWORD tid = GetWindowThreadProcessId(h, NULL);
    if (tid) {
        GUITHREADINFO gti = { sizeof(GUITHREADINFO) };
        if (GetGUIThreadInfo(tid, &gti) &&
            (gti.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE)))
            return;
    }

    HWND insertAfter = IsBaseLayerWindow(h) ? HWND_NOTOPMOST : HWND_TOPMOST;
    SetWindowPos(h, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// Standard workaround for SetForegroundWindow's foreground-lock restriction:
// if a plain call doesn't take effect, briefly attach our input thread to
// the current foreground thread (which grants the right to call it) and
// retry.
static void ForceSetForegroundWindow(HWND h) {
    if (SetForegroundWindow(h)) return;

    HWND curFg = GetForegroundWindow();
    if (!curFg || curFg == h) return;

    DWORD curThread = GetWindowThreadProcessId(curFg, NULL);
    DWORD targetThread = GetWindowThreadProcessId(h, NULL);
    DWORD myThread = GetCurrentThreadId();

    BOOL attachedCur = FALSE;
    BOOL attachedTarget = FALSE;

    if (curThread && curThread != myThread) {
        attachedCur = AttachThreadInput(myThread, curThread, TRUE);
    }
    if (targetThread && targetThread != myThread && targetThread != curThread) {
        attachedTarget = AttachThreadInput(myThread, targetThread, TRUE);
    }

    if (!SetForegroundWindow(h)) {
        // Fallback: simulate an Alt press to bypass the foreground lock.
        // MARK_INJECTED is essential here — without it our own keyboard hook
        // sees these as real physical keypresses and could accidentally fire
        // an Alt+<key> hotkey if another key happened to land simultaneously.
        keybd_event(VK_MENU, 0, 0, MARK_INJECTED);
        SetForegroundWindow(h);
        keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, MARK_INJECTED);
    }
    BringWindowToTop(h);

    if (attachedTarget) {
        AttachThreadInput(myThread, targetThread, FALSE);
    }
    if (attachedCur) {
        AttachThreadInput(myThread, curThread, FALSE);
    }
}

void ActivateForFocusFollow(HWND h) {
    SyncWindowLayering(h);
    ForceSetForegroundWindow(h);
}

// ============================================================================
//  Focus-follows-mouse — driven by a WH_MOUSE_LL hook, per the architecture
//  notes at the top of main.c (no polling timer).
//
//  MouseHookProc does the minimum possible work, per the same low-level-hook
//  budget rule the keyboard hook follows: a cheap integer comparison against
//  the last seen point, then PostMessage and return. All the real work
//  (WindowFromPoint, GetAncestor, SetForegroundWindow, ...) happens in
//  FocusFollowsMouse() on the main thread after WM_APP_MOUSEMOVE arrives.
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

// Called on the main message loop thread in response to WM_APP_MOUSEMOVE.
void FocusFollowsMouse(void) {
    static HWND lastHwnd = NULL;
    static LONG lastX = -999999, lastY = -999999;

    if (GetTickCount() - g_lastAltTabTick < 400) return;

    POINT pt;
    GetCursorPos(&pt);

    // Skip if position hasn't changed (AHK's lastX/lastY guard).
    if (pt.x == lastX && pt.y == lastY) return;
    lastX = pt.x;
    lastY = pt.y;

    HWND h = WindowFromPoint(pt);
    if (!h || h == lastHwnd) return;

    if (ShouldIgnoreFocusWindow(h)) return;

    h = GetAncestor(h, GA_ROOT);
    if (!h || h == lastHwnd) return;
    if (ShouldIgnoreFocusWindow(h)) return;

    HWND active = GetForegroundWindow();
    if (active && h == active) { lastHwnd = h; return; }

    if (!IsWindow(h)) return;

    lastHwnd = h;
    ActivateForFocusFollow(h);
}

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
        default:
            break;
    }
}
