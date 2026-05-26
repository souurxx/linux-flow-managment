#Requires AutoHotkey v2.0
#SingleInstance Force
Persistent

SetWinDelay(0)
SetControlDelay(0)
SendMode "Input"

; ── Auto-elevate to admin ─────────────────────────────────────────────
full_command_line := DllCall("GetCommandLine", "str")
if not (A_IsAdmin or RegExMatch(full_command_line, " /restart(?!\S)")) {
    Run '*RunAs "' A_AhkPath '" /restart "' A_ScriptFullPath '"'
    ExitApp
}

; ── Load Virtual Desktop helper DLL ──────────────────────────────────
DllPath := A_ScriptDir "\VirtualDesktopAccessor.dll"
hVDA := DllCall("LoadLibrary", "Str", DllPath, "Ptr")
if !hVDA {
    MsgBox "Could not load VirtualDesktopAccessor.dll`nCheck the path: " DllPath
    ExitApp
}

; ── State ─────────────────────────────────────────────────────────────
global PrevPos := Map()
global AltWState := Map()
global AltTabCooldown := 0

SetTimer(CleanupWindowState, 5000)

; ── Auto-hide RoundedTB on startup ────────────────────────────────────
SetTimer(HideRoundedTB, 500)

HideRoundedTB() {
    static tries := 0
    tries++

    if tries > 60 {
        SetTimer(HideRoundedTB, 0)
        return
    }

    DetectHiddenWindows(false)
    if WinExist("ahk_exe RoundedTB.exe") {
        WinHide("ahk_exe RoundedTB.exe")
        SetTimer(HideRoundedTB, 0)
    }
}

; ── Spawn new windows under mouse cursor ──────────────────────────────
DllCall("RegisterShellHookWindow", "Ptr", A_ScriptHwnd)
OnMessage(DllCall("RegisterWindowMessage", "Str", "SHELLHOOK"), ShellMessage)

ShellMessage(wParam, lParam, msg, hwnd) {
    static WS_EX_TOOLWINDOW := 0x00000080
    static WS_EX_NOACTIVATE := 0x08000000

    if (wParam = 1) {
        Sleep 50

        try {
            if !WinExist("ahk_id " lParam)
                return

            style := WinGetExStyle("ahk_id " lParam)
            if (style & WS_EX_TOOLWINDOW) || (style & WS_EX_NOACTIVATE)
                return

            processName := WinGetProcessName("ahk_id " lParam)
            if ShouldIgnoreNewWindowProcess(processName)
                return

            className := WinGetClass("ahk_id " lParam)
            if IsShellClass(className) || IsPopupClass(className)
                return

            if WinGetMinMax("ahk_id " lParam) != 0
                return

            if (className = "CabinetWClass")
                Sleep 30

            WinGetPos &winX, &winY, &winW, &winH, "ahk_id " lParam
            if (winX < -30000 || winY < -30000)
                return

            if (winW < 100 || winH < 100)
                return

            CoordMode "Mouse", "Screen"
            MouseGetPos &mouseX, &mouseY

            targetX := mouseX - (winW / 2)
            targetY := mouseY - (winH / 2)

            monIndex := 1
            count := MonitorGetCount()
            Loop count {
                MonitorGetWorkArea(A_Index, &l, &t, &r, &b)
                if (mouseX >= l && mouseX < r && mouseY >= t && mouseY < b) {
                    monIndex := A_Index
                    break
                }
            }

            MonitorGetWorkArea(monIndex, &ml, &mt, &mr, &mb)
            targetX := Max(ml, Min(targetX, mr - winW))
            targetY := Max(mt, Min(targetY, mb - winH))

            try WinMove targetX, targetY,,, "ahk_id " lParam
        }
    }
}

ShouldIgnoreNewWindowProcess(processName) {
    switch processName {
        case "zebar.exe", "glazewm.exe", "Telegram.exe", "steam.exe",
             "steamwebhelper.exe", "wallpaper64.exe", "obs64.exe", "Discord.exe":
            return true
    }
    return false
}

; ── Focus follows mouse ───────────────────────────────────────────────
SetTimer(FocusFollowsMouse, 25)

FocusFollowsMouse() {
    global AltTabCooldown

    static lastHwnd := 0
    static lastX := 0
    static lastY := 0

    if GetKeyState("Alt", "P")
        return

    if (A_TickCount - AltTabCooldown < 400)
        return

    CoordMode "Mouse", "Screen"
    MouseGetPos &curX, &curY, &hWnd

    if (curX = lastX && curY = lastY)
        return

    lastX := curX
    lastY := curY

    if !hWnd || hWnd = lastHwnd
        return

    if ShouldIgnoreFocusWindow(hWnd)
        return

    hWnd := DllCall("GetAncestor", "Ptr", hWnd, "UInt", 2, "Ptr")

    if !hWnd || hWnd = lastHwnd
        return

    if ShouldIgnoreFocusWindow(hWnd)
        return

    activeHwnd := GetActiveWindowID()
    if (activeHwnd && hWnd = activeHwnd) {
        lastHwnd := hWnd
        return
    }

    if !WinExist("ahk_id " hWnd)
        return

    lastHwnd := hWnd
    try WinActivate("ahk_id " hWnd)
}

ShouldIgnoreFocusWindow(hWnd) {
    static WS_EX_TOOLWINDOW := 0x00000080
    static WS_EX_NOACTIVATE := 0x08000000
    static WS_POPUP := 0x80000000
    static GW_OWNER := 4

    try {
        if !WinExist("ahk_id " hWnd)
            return true

        if !DllCall("IsWindowVisible", "Ptr", hWnd)
            return true

        class := WinGetClass("ahk_id " hWnd)
        if IsShellClass(class) || IsPopupClass(class)
            return true

        exStyle := WinGetExStyle("ahk_id " hWnd)
        if (exStyle & WS_EX_TOOLWINDOW) || (exStyle & WS_EX_NOACTIVATE)
            return true

        style := WinGetStyle("ahk_id " hWnd)
        owner := DllCall("GetWindow", "Ptr", hWnd, "UInt", GW_OWNER, "Ptr")
        if owner && (style & WS_POPUP)
            return true

        processName := WinGetProcessName("ahk_id " hWnd)
        if IsIgnoredUtilityProcess(processName)
            return true
    }

    return false
}

IsShellClass(className) {
    switch className {
        case "WorkerW", "Progman", "Shell_TrayWnd", "Shell_SecondaryTrayWnd":
            return true
    }
    return false
}

IsPopupClass(className) {
    switch className {
        case "#32768",
             "MozillaDropShadowWindowClass",
             "MozillaPopupWindowClass",
             "Xaml_WindowedPopupClass",
             "Windows.UI.Core.CoreWindow",
             "Microsoft.UI.Content.PopupWindowSiteBridge",
             "AutoSuggestBoxPopupWindowClass",
             "tooltips_class32",
             "SysShadow":
            return true
    }
    return false
}

IsIgnoredUtilityProcess(processName) {
    switch processName {
        case "AltSnap.exe",
             "StartMenuExperienceHost.exe",
             "SearchHost.exe",
             "ShellExperienceHost.exe",
             "TextInputHost.exe",
             "steamwebhelper.exe":
            return true
    }
    return false
}

; ── Track Alt+Tab for cooldown ────────────────────────────────────────
~!Tab:: AltTabCooldown := A_TickCount

; ── General helpers ───────────────────────────────────────────────────
GetActiveWindowID() {
    try return WinGetID("A")
    return 0
}

GetTargetWindow() {
    MouseGetPos(,, &hWnd)

    if !hWnd {
        try hWnd := WinGetID("A")
        catch
            return 0
    }

    hWnd := DllCall("GetAncestor", "Ptr", hWnd, "UInt", 2, "Ptr")
    return hWnd
}

IsFirefoxWindow(hwnd) {
    try return WinGetProcessName("ahk_id " hwnd) = "firefox.exe"
    return false
}

CanMoveToDesktop(hWnd) {
    if !hWnd
        return false

    try {
        class := WinGetClass("ahk_id " hWnd)
        if IsShellClass(class) || IsPopupClass(class)
            return false

        processName := WinGetProcessName("ahk_id " hWnd)
        if IsIgnoredUtilityProcess(processName)
            return false
    } catch {
        return false
    }

    return true
}

GoToDesktop(desktopNumber) {
    DllCall("VirtualDesktopAccessor\GoToDesktopNumber", "Int", desktopNumber)
}

MoveTargetWindowToDesktop(desktopNumber) {
    hWnd := GetTargetWindow()
    if !CanMoveToDesktop(hWnd)
        return

    ClearWindowState(hWnd, true)
    DllCall("VirtualDesktopAccessor\MoveWindowToDesktopNumber", "Ptr", hWnd, "Int", desktopNumber)
}

MoveTargetWindowAndGoToDesktop(desktopNumber) {
    hWnd := GetTargetWindow()
    if !CanMoveToDesktop(hWnd)
        return

    ClearWindowState(hWnd, true)
    DllCall("VirtualDesktopAccessor\MoveWindowToDesktopNumber", "Ptr", hWnd, "Int", desktopNumber)
    GoToDesktop(desktopNumber)
}

; ── Window state helpers ──────────────────────────────────────────────
CleanupWindowState() {
    global PrevPos, AltWState

    stale := []
    for key, _ in AltWState {
        if !WinExist("ahk_id " key)
            stale.Push(key)
    }
    for _, key in stale {
        if AltWState.Has(key)
            AltWState.Delete(key)
        if PrevPos.Has(key)
            PrevPos.Delete(key)
    }

    stale := []
    for key, _ in PrevPos {
        if !WinExist("ahk_id " key)
            stale.Push(key)
    }
    for _, key in stale {
        if PrevPos.Has(key)
            PrevPos.Delete(key)
    }
}

ClearWindowState(hwnd, restore := false) {
    global PrevPos, AltWState

    key := String(hwnd)

    if restore && AltWState.Has(key) && AltWState[key] = 2 && PrevPos.Has(key) {
        RestoreWindowState(hwnd)
        return
    }

    if PrevPos.Has(key)
        PrevPos.Delete(key)

    if AltWState.Has(key)
        AltWState.Delete(key)
}

RestoreWindowState(hwnd) {
    global PrevPos, AltWState

    static GWL_STYLE := -16
    static GWL_EXSTYLE := -20
    static SWP_FRAMECHANGED := 0x0020
    static SWP_NOZORDER := 0x0004
    static SWP_NOACTIVATE := 0x0010
    static SWP_NOREDRAW := 0x0008
    static SWP_NOCOPYBITS := 0x0100

    key := String(hwnd)

    if !PrevPos.Has(key) {
        try WinSetAlwaysOnTop(0, "ahk_id " hwnd)
        try WinRestore("ahk_id " hwnd)

        if AltWState.Has(key)
            AltWState.Delete(key)

        return
    }

    pos := PrevPos[key]

    try {
        DllCall("SetWindowLongPtr", "Ptr", hwnd, "Int", GWL_STYLE, "Ptr", pos[6], "Ptr")
        DllCall("SetWindowLongPtr", "Ptr", hwnd, "Int", GWL_EXSTYLE, "Ptr", pos[7], "Ptr")

        WinSetAlwaysOnTop(pos[5] ? 1 : 0, "ahk_id " hwnd)

        if WinGetMinMax("ahk_id " hwnd) != 0
            WinRestore("ahk_id " hwnd)

        WinMove(pos[1], pos[2], pos[3], pos[4], "ahk_id " hwnd)

        DllCall("SetWindowPos", "Ptr", hwnd, "Ptr", 0,
            "Int", pos[1], "Int", pos[2], "Int", pos[3], "Int", pos[4],
            "UInt", SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOREDRAW | SWP_NOCOPYBITS)

        DllCall("RedrawWindow", "Ptr", hwnd, "Ptr", 0, "Ptr", 0, "UInt", 0x85)
    }

    if PrevPos.Has(key)
        PrevPos.Delete(key)

    if AltWState.Has(key)
        AltWState.Delete(key)
}

; ── Helper: check if focused window is a real app ─────────────────────
IsRealWindow() {
    hWnd := GetActiveWindowID()
    if !hWnd
        return false

    try {
        class := WinGetClass("ahk_id " hWnd)
        if IsShellClass(class)
            return false

        processName := WinGetProcessName("ahk_id " hWnd)
        if IsIgnoredUtilityProcess(processName)
            return false
    } catch {
        return false
    }

    return true
}

; ── Monitor helpers ───────────────────────────────────────────────────
GetMonitorWorkAreaForWindow(hwnd, &ml, &mt, &mr, &mb) {
    WinGetPos(&wx, &wy, &ww, &wh, "ahk_id " hwnd)
    cx := wx + ww // 2
    cy := wy + wh // 2

    count := MonitorGetCount()
    Loop count {
        MonitorGetWorkArea(A_Index, &l, &t, &r, &b)
        if (cx >= l && cx < r && cy >= t && cy < b) {
            ml := l, mt := t, mr := r, mb := b
            return
        }
    }

    MonitorGetWorkArea(1, &ml, &mt, &mr, &mb)
}

GetMonitorBoundsForWindow(hwnd, &ml, &mt, &mr, &mb) {
    WinGetPos(&wx, &wy, &ww, &wh, "ahk_id " hwnd)
    cx := wx + ww // 2
    cy := wy + wh // 2

    count := MonitorGetCount()
    Loop count {
        MonitorGet(A_Index, &l, &t, &r, &b)
        if (cx >= l && cx < r && cy >= t && cy < b) {
            ml := l, mt := t, mr := r, mb := b
            return
        }
    }

    MonitorGet(1, &ml, &mt, &mr, &mb)
}

GetVisibleFrameOffsets(hwnd, &leftOffset, &topOffset, &rightOffset, &bottomOffset) {
    leftOffset := 0
    topOffset := 0
    rightOffset := 0
    bottomOffset := 0

    try {
        WinGetPos(&wx, &wy, &ww, &wh, "ahk_id " hwnd)

        rect := Buffer(16, 0)
        result := DllCall("dwmapi\DwmGetWindowAttribute",
            "Ptr", hwnd,
            "UInt", 9,
            "Ptr", rect,
            "UInt", 16,
            "Int")

        if (result = 0) {
            vl := NumGet(rect, 0, "Int")
            vt := NumGet(rect, 4, "Int")
            vr := NumGet(rect, 8, "Int")
            vb := NumGet(rect, 12, "Int")

            leftOffset := Max(0, vl - wx)
            topOffset := Max(0, vt - wy)
            rightOffset := Max(0, (wx + ww) - vr)
            bottomOffset := Max(0, (wy + wh) - vb)
        }
    }
}

MoveWindowVisibleRect(hwnd, l, t, r, b) {
    GetVisibleFrameOffsets(hwnd, &lo, &to, &ro, &bo)

    WinMove(
        l - lo,
        t - to,
        (r - l) + lo + ro,
        (b - t) + to + bo,
        "ahk_id " hwnd
    )
}

RestoreWindowDirectlyToRect(hwnd, l, t, r, b) {
    wp := Buffer(44, 0)
    NumPut("UInt", 44, wp, 0)

    if !DllCall("GetWindowPlacement", "Ptr", hwnd, "Ptr", wp)
        return false

    NumPut("UInt", 1, wp, 8)
    NumPut("Int", l, wp, 28)
    NumPut("Int", t, wp, 32)
    NumPut("Int", r, wp, 36)
    NumPut("Int", b, wp, 40)

    return DllCall("SetWindowPlacement", "Ptr", hwnd, "Ptr", wp)
}

; ── Snap helpers ──────────────────────────────────────────────────────
SnapLeft() {
    hwnd := GetActiveWindowID()
    if !hwnd
        return

    key := String(hwnd)
    if AltWState.Has(key) && AltWState[key] = 2 && IsFirefoxWindow(hwnd) {
        Send "{F11}"
        Sleep 150
    }

    ClearWindowState(hwnd, false)

    GetMonitorWorkAreaForWindow(hwnd, &l, &t, &r, &b)
    w := r - l

    targetL := l
    targetT := t
    targetR := l + (w // 2)
    targetB := b

    if WinGetMinMax("ahk_id " hwnd) != 0
        RestoreWindowDirectlyToRect(hwnd, targetL, targetT, targetR, targetB)

    MoveWindowVisibleRect(hwnd, targetL, targetT, targetR, targetB)
}

SnapRight() {
    hwnd := GetActiveWindowID()
    if !hwnd
        return

    key := String(hwnd)
    if AltWState.Has(key) && AltWState[key] = 2 && IsFirefoxWindow(hwnd) {
        Send "{F11}"
        Sleep 150
    }

    ClearWindowState(hwnd, false)

    GetMonitorWorkAreaForWindow(hwnd, &l, &t, &r, &b)
    w := r - l

    targetL := l + (w // 2)
    targetT := t
    targetR := r
    targetB := b

    if WinGetMinMax("ahk_id " hwnd) != 0
        RestoreWindowDirectlyToRect(hwnd, targetL, targetT, targetR, targetB)

    MoveWindowVisibleRect(hwnd, targetL, targetT, targetR, targetB)
}

; ╔═══════════════════════════════════════════════════════════════════╗
;  HOTKEYS
; ╚═══════════════════════════════════════════════════════════════════╝

; ── App launchers ─────────────────────────────────────────────────────
!s::     Run "firefox.exe"
!x::     Run "explorer.exe"
!n::     Run "notepad.exe"
!Enter:: Run "wt.exe"

!Space:: {
    bravePath := "C:\Program Files\BraveSoftware\Brave-Browser\Application\brave.exe"

    if WinExist("ahk_exe brave.exe") {
        WinActivate("ahk_exe brave.exe")
        if WinWaitActive("ahk_exe brave.exe",, 2)
            Send "^n"
        return
    }

    if FileExist(bravePath)
        Run 'explorer.exe "' bravePath '"'
    else
        MsgBox "Could not find Brave at:`n" bravePath
}

; ── Window management ─────────────────────────────────────────────────
!z:: {
    if IsRealWindow() {
        hWnd := GetActiveWindowID()
        if hWnd {
            ClearWindowState(hWnd, true)
            WinClose("ahk_id " hWnd)
        }
    }
}

!c:: {
    if IsRealWindow() {
        hWnd := GetActiveWindowID()
        if hWnd {
            ClearWindowState(hWnd, true)
            WinMinimize("ahk_id " hWnd)
        }
    }
}

!q:: {
    if IsRealWindow()
        SnapLeft()
}

!e:: {
    if IsRealWindow()
        SnapRight()
}

; ── Alt+W → maximize → fake fullscreen → restore ─────────────────────
!w:: {
    global PrevPos, AltWState

    static GWL_STYLE := -16
    static GWL_EXSTYLE := -20
    static WS_CAPTION := 0x00C00000
    static WS_THICKFRAME := 0x00040000
    static SWP_FRAMECHANGED := 0x0020
    static SWP_NOZORDER := 0x0004
    static SWP_NOACTIVATE := 0x0010
    static SWP_NOREDRAW := 0x0008
    static SWP_NOCOPYBITS := 0x0100

    if !IsRealWindow()
        return

    hWnd := GetActiveWindowID()
    if !hWnd
        return

    if (WinGetMinMax("ahk_id " hWnd) = -1) {
        WinRestore("ahk_id " hWnd)
        Sleep 100
        WinMaximize("ahk_id " hWnd)
        key := String(hWnd)
        AltWState[key] := 1
        return
    }

    key := String(hWnd)
    state := AltWState.Has(key) ? AltWState[key] : 0

    ; ── Fix 3: sync state with Firefox's actual fullscreen ───────────
    if IsFirefoxWindow(hWnd) {
        style := WinGetStyle("ahk_id " hWnd)
        isActuallyFullscreen := !(style & WS_CAPTION) && !(style & WS_THICKFRAME)
        if isActuallyFullscreen && state != 2
            state := 2
        else if !isActuallyFullscreen && state = 2
            state := 0
    }

    if (state = 1 && WinGetMinMax("ahk_id " hWnd) != 1)
        state := 0

    if (state = 0) {
        WinGetPos(&x, &y, &w, &h, "ahk_id " hWnd)
        wasTopmost := (WinGetExStyle("ahk_id " hWnd) & 0x8) != 0
        style := WinGetStyle("ahk_id " hWnd)
        exStyle := WinGetExStyle("ahk_id " hWnd)
        PrevPos[key] := [x, y, w, h, wasTopmost, style, exStyle]
        WinMaximize("ahk_id " hWnd)
        AltWState[key] := 1
        return
    }

    if (state = 1) {
        if IsFirefoxWindow(hWnd) {
            Send "{F11}"
            Sleep 150
            AltWState[key] := 2
            return
        }

        GetMonitorBoundsForWindow(hWnd, &l, &t, &r, &b)

        if !PrevPos.Has(key) {
            WinGetPos(&x, &y, &w, &h, "ahk_id " hWnd)
            wasTopmost := (WinGetExStyle("ahk_id " hWnd) & 0x8) != 0
            style := WinGetStyle("ahk_id " hWnd)
            exStyle := WinGetExStyle("ahk_id " hWnd)
            PrevPos[key] := [x, y, w, h, wasTopmost, style, exStyle]
        }

        style := WinGetStyle("ahk_id " hWnd)
        newStyle := style & ~WS_CAPTION & ~WS_THICKFRAME

        wp := Buffer(44, 0)
        NumPut("UInt", 44, wp, 0)
        DllCall("GetWindowPlacement", "Ptr", hWnd, "Ptr", wp)
        NumPut("UInt", 1, wp, 8)
        NumPut("Int", l, wp, 28)
        NumPut("Int", t, wp, 32)
        NumPut("Int", r, wp, 36)
        NumPut("Int", b, wp, 40)

        DllCall("SetWindowLongPtr", "Ptr", hWnd, "Int", GWL_STYLE, "Ptr", newStyle, "Ptr")
        DllCall("SetWindowPlacement", "Ptr", hWnd, "Ptr", wp)
        WinSetAlwaysOnTop(1, "ahk_id " hWnd)

        DllCall("SetWindowPos", "Ptr", hWnd, "Ptr", 0,
            "Int", l, "Int", t, "Int", r - l, "Int", b - t,
            "UInt", SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOREDRAW | SWP_NOCOPYBITS)

        DllCall("RedrawWindow", "Ptr", hWnd, "Ptr", 0, "Ptr", 0, "UInt", 0x85)

        AltWState[key] := 2
        return
    }

    if (state = 2) {
        if IsFirefoxWindow(hWnd) {
            Send "{F11}"
            Sleep 120
        }
        RestoreWindowState(hWnd)
    }
}

; ── Fake fullscreen escape ────────────────────────────────────────────
!Esc:: {
    global AltWState

    hWnd := GetActiveWindowID()
    if !hWnd
        return

    key := String(hWnd)
    if AltWState.Has(key) && AltWState[key] = 2
        RestoreWindowState(hWnd)
}

; ── Debug active window ───────────────────────────────────────────────
!i:: {
    hWnd := GetActiveWindowID()
    if !hWnd {
        MsgBox "No active window."
        return
    }

    title := WinGetTitle("ahk_id " hWnd)
    class := WinGetClass("ahk_id " hWnd)
    processName := WinGetProcessName("ahk_id " hWnd)
    minMax := WinGetMinMax("ahk_id " hWnd)
    style := WinGetStyle("ahk_id " hWnd)
    exStyle := WinGetExStyle("ahk_id " hWnd)

    info := "Title: " title
        . "`nClass: " class
        . "`nProcess: " processName
        . "`nHwnd: " hWnd
        . "`nMinMax: " minMax
        . "`nStyle: " Format("0x{:X}", style)
        . "`nExStyle: " Format("0x{:X}", exStyle)

    A_Clipboard := info
    MsgBox info
}

; ── Virtual desktops ──────────────────────────────────────────────────
!1:: GoToDesktop(0)
!2:: GoToDesktop(1)
!3:: GoToDesktop(2)
!4:: MoveTargetWindowAndGoToDesktop(3)

!a:: MoveTargetWindowToDesktop(0)
!d:: MoveTargetWindowToDesktop(1)
!f:: MoveTargetWindowToDesktop(2)

; ── Firefox tab control ───────────────────────────────────────────────
#HotIf WinActive("ahk_exe firefox.exe")
^Space:: Send "^t"
^f::     Send "^w"
#HotIf

; ── Brave tab control ─────────────────────────────────────────────────
#HotIf WinActive("ahk_exe brave.exe")
^Space:: Send "^t"
^f::     Send "^w"
#HotIf

