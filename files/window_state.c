// ============================================================================
//  window_state.c — HWND -> saved geometry/style hash table, with identity
//  verification to defend against HWND reuse.
//
//  Windows recycles HWND values once a window is destroyed, so a stale
//  entry could otherwise silently get applied to an unrelated new window
//  that happens to reuse the same handle. Every entry also stores the
//  window's class name + process name at save time; before any saved state
//  is *used* (not just before periodic cleanup), FindVerifiedNode() re-checks
//  that the current window at that HWND still has the same class+process —
//  if not, the stale entry is unlinked from the table immediately and
//  treated as nonexistent (see the comment on FindVerifiedNode below).
// ============================================================================

#include "wm_common.h"

#define WS_BUCKETS 257
static WSNode* g_stateTable[WS_BUCKETS];

static unsigned HashHwnd(HWND h) {
    ULONG_PTR v = (ULONG_PTR)h;
    return (unsigned)((v >> 3) % WS_BUCKETS);
}

static void RemoveNode(HWND h) {
    unsigned b = HashHwnd(h);
    WSNode** pp = &g_stateTable[b];
    while (*pp) {
        if ((*pp)->hwnd == h) {
            WSNode* dead = *pp;
            *pp = dead->next;
            free(dead);
            return;
        }
        pp = &(*pp)->next;
    }
}

// Returns the node only if it exists AND still matches the live window's
// class+process (defends against HWND reuse). Logs + drops on mismatch.
//
// On mismatch the stale node is unlinked from the hash table immediately
// (not just logged) — otherwise a recycled HWND would keep hitting this
// same dead entry on every future lookup until the next periodic
// CleanupStaleNodes() pass, silently growing the table in the meantime.
WSNode* FindVerifiedNode(HWND h) {
    unsigned b = HashHwnd(h);
    for (WSNode* n = g_stateTable[b]; n; n = n->next) {
        if (n->hwnd != h) continue;

        wchar_t curClass[64], curProc[MAX_PATH];
        GetWindowClassSafe(h, curClass, 64);
        GetWindowProcessNameSafe(h, curProc, MAX_PATH);

        if (wcscmp(curClass, n->className) != 0 || wcscmp(curProc, n->processName) != 0) {
            LogMsg(L"State for hwnd 0x%p discarded: identity mismatch "
                   L"(saved '%ls'/'%ls', now '%ls'/'%ls') — likely HWND reuse.",
                   h, n->className, n->processName, curClass, curProc);
            RemoveNode(h);
            return NULL;
        }
        return n;
    }
    return NULL;
}

// GetOrCreateNode — find or allocate the state node for h.
//
// Bug #6 fix: we now call FindVerifiedNode first instead of scanning the
// bucket blindly.  If an entry already exists for this HWND but the
// class+process has changed (HWND reuse), FindVerifiedNode removes the stale
// node so we allocate a fresh one with the correct identity.  Without this,
// a recycled HWND returned a node belonging to a dead window, and any
// geometry/style written into it was attributed to the wrong window.
WSNode* GetOrCreateNode(HWND h) {
    // Try verified lookup first — handles both the normal "already exists"
    // case and the HWND-reuse case (stale node gets dropped inside).
    WSNode* existing = FindVerifiedNode(h);
    if (existing) return existing;

    // Either no node existed, or FindVerifiedNode just evicted a stale one.
    // Allocate a fresh node with current identity.
    WSNode* n = (WSNode*)calloc(1, sizeof(WSNode));
    if (!n) return NULL;
    n->hwnd = h;
    GetWindowClassSafe(h, n->className, 64);
    GetWindowProcessNameSafe(h, n->processName, MAX_PATH);

    unsigned b = HashHwnd(h);
    n->next = g_stateTable[b];
    g_stateTable[b] = n;
    return n;
}

void CleanupStaleNodes(void) {
    for (int b = 0; b < WS_BUCKETS; b++) {
        WSNode** pp = &g_stateTable[b];
        while (*pp) {
            if (!IsWindow((*pp)->hwnd)) {
                WSNode* dead = *pp;
                *pp = dead->next;
                free(dead);
            } else {
                pp = &(*pp)->next;
            }
        }
    }
}

void ClearWindowState(HWND h, BOOL allowRestore) {
    WSNode* n = FindVerifiedNode(h);
    if (!n) { RemoveNode(h); return; }

    if (allowRestore && n->altState == ALTWSTATE_FULLSCREEN && n->hasPos) {
        // caller will invoke RestoreWindowState separately; just leave node intact
        return;
    }
    RemoveNode(h);
}

void RestoreWindowState(HWND h) {
    // Always show the taskbar first — even if we have no saved node (e.g.
    // after a crash/restart where state was lost), we must not leave the
    // taskbar hidden permanently.
    ShowTaskbarForWindow(h);

    WSNode* n = FindVerifiedNode(h);
    if (!n || !n->hasPos) {
        // No saved geometry.  Promote/demote topmost based on whether the
        // window was originally topmost (wasTopmost).  Fall back to NOTOPMOST
        // if we have no node at all, which is the safe default for a window
        // whose state we never recorded.
        HWND insertAfter = (n && n->wasTopmost) ? HWND_TOPMOST : HWND_NOTOPMOST;
        SetWindowPos(h, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(h, SW_RESTORE);
        if (n) RemoveNode(h);
        return;
    }

    // Restore original style first so the subsequent size/move uses the
    // correct frame metrics.
    SetWindowLongPtrW(h, GWL_STYLE,   n->style);
    SetWindowLongPtrW(h, GWL_EXSTYLE, n->exStyle);

    // Bug #3 fix: restore the window to the topmost band it was in BEFORE we
    // touched it.  The original code unconditionally used HWND_TOPMOST, which
    // permanently promoted every restored window regardless of its original
    // z-order.  A normal non-topmost app that went through fake-fullscreen
    // would come back as topmost and sit on top of everything else forever.
    HWND insertAfter = n->wasTopmost ? HWND_TOPMOST : HWND_NOTOPMOST;
    SetWindowPos(h, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    if (IsIconic(h) || IsZoomed(h))
        ShowWindow(h, SW_RESTORE);

    // Two-pass size restore: MoveWindow for the basic geometry, then
    // SetWindowPos with SWP_FRAMECHANGED to flush the new style into DWM.
    MoveWindow(h, n->x, n->y, n->w, n->h, FALSE);

    SetWindowPos(h, NULL, n->x, n->y, n->w, n->h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED |
                 SWP_NOREDRAW | SWP_NOCOPYBITS);

    RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

    RemoveNode(h);
}

// Frees every entry in the window-state table. Called once at shutdown.
void FreeAllWindowState(void) {
    for (int b = 0; b < WS_BUCKETS; b++) {
        WSNode* n = g_stateTable[b];
        while (n) { WSNode* next = n->next; free(n); n = next; }
        g_stateTable[b] = NULL;
    }
}
