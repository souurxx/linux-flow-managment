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

WSNode* GetOrCreateNode(HWND h) {
    unsigned b = HashHwnd(h);
    for (WSNode* n = g_stateTable[b]; n; n = n->next)
        if (n->hwnd == h) return n;

    WSNode* n = (WSNode*)calloc(1, sizeof(WSNode));
    if (!n) return NULL;
    n->hwnd = h;
    GetWindowClassSafe(h, n->className, 64);
    GetWindowProcessNameSafe(h, n->processName, MAX_PATH);
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
    ShowTaskbarForWindow(h);

    WSNode* n = FindVerifiedNode(h);
    if (!n || !n->hasPos) {
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(h, SW_RESTORE);
        if (n) RemoveNode(h);
        return;
    }

    SetWindowLongPtrW(h, GWL_STYLE, n->style);
    SetWindowLongPtrW(h, GWL_EXSTYLE, n->exStyle);

    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    if (IsIconic(h) || IsZoomed(h))
        ShowWindow(h, SW_RESTORE);

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
