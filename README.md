# wm — Windows Manager

Linux-style window management for Windows. Keyboard-driven focus, virtual desktops, snap, and spawn-under-mouse — written in native C, no runtime required.

> Rewritten from AutoHotkey to native Win32 C for zero overhead, proper event-driven architecture, and no AHK dependency.

---

## Features

- **Focus follows mouse** — hover over a window to focus it, no click needed. Driven by a low-level mouse hook, not a polling timer — idle CPU usage is effectively zero
- **Spawn under mouse** — new windows appear centered on your cursor automatically
- **Virtual desktops** — switch instantly with Alt+1/2/3, move windows silently to any desktop
- **Snap** — half-screen left/right on any monitor with proper DWM frame compensation
- **Maximize / fake fullscreen** — separate hotkeys, both with clean restore
- **Smart z-order** — maximized and fullscreen windows are managed to never bury floating utilities, while third-party overlays (media viewers, video players) are left alone
- **Config file** — `wm.ini` next to the exe, auto-created on first run. Add/remove ignored processes and window classes without recompiling
- **Log file** — `wm.log` next to the exe with timestamps. Rotates automatically at 2MB
- **Tray icon** — right-click to view log or exit. No Task Manager needed
- **Single instance** — launching a second copy gracefully replaces the first

---

## Requirements

- Windows 10 or 11
- Run as Administrator (the exe prompts automatically via UAC)
- `VirtualDesktopAccessor.dll` placed next to `wm.exe` for virtual desktop hotkeys (Alt+1/2/3/4/A/D/F). Without it the app still runs — those hotkeys are just disabled

---

## Installation

1. Download `wm.exe` and the correct DLL for your Windows version:
   - **Windows 10:** `VirtualDesktopAccessor.dll`
   - **Windows 11:** `VirtualDesktopAccessorwin11.dll` — rename it to `VirtualDesktopAccessor.dll`
2. Put both files in the same folder
3. Run `wm.exe` — UAC will prompt, accept it
4. The tray icon confirms it's running

To launch on startup: press `Win+R`, type `shell:startup`, press Enter, and drop a shortcut to `wm.exe` in that folder.

> **Virtual desktops:** create them yourself first via `Win+Tab → New Desktop`. The app supports up to 4. You can't switch to a desktop that doesn't exist yet.

---

## Hotkeys

| Hotkey | Action |
|---|---|
| `Alt+1` / `2` / `3` | Switch to virtual desktop 1 / 2 / 3 |
| `Alt+4` | Move focused window to desktop 4 and switch there |
| `Alt+A` | Move focused window to desktop 1 (stay on current desktop) |
| `Alt+D` | Move focused window to desktop 2 (stay on current desktop) |
| `Alt+F` | Move focused window to desktop 3 (stay on current desktop) |
| `Alt+W` | Toggle maximize / restore |
| `Alt+V` | Toggle fake fullscreen / restore |
| `Alt+Esc` | Escape fake fullscreen |
| `Alt+Q` | Snap left (half screen) |
| `Alt+E` | Snap right (half screen) |
| `Alt+Z` | Close window |
| `Alt+C` | Minimize window |
| `Alt+S` | Open Firefox |
| `Alt+X` | Open File Explorer |
| `Alt+N` | Open Notepad |
| `Alt+Enter` | Open Terminal (`wt.exe`) |
| `Alt+Space` | Open Brave (new window if already running) |
| `Alt+I` | Debug info for focused window (copies to clipboard) |
| `Ctrl+Space` | New tab (Firefox / Brave only) |
| `Ctrl+F` | Close tab (Firefox / Brave only) |

---

## Configuration

`wm.ini` is created automatically next to `wm.exe` on first run. Edit it to customize behavior — no recompile needed, just restart `wm.exe`.

```ini
[Settings]
FocusFollowsMouse=1   ; 1 = enabled, 0 = disabled
HideRoundedTB=1       ; 1 = hide RoundedTB window on startup, 0 = no

[IgnoreProcesses]
; Windows under these processes are ignored by focus-follow and hotkeys
proc1=AltSnap.exe
proc2=StartMenuExperienceHost.exe
proc3=SearchHost.exe
proc4=ShellExperienceHost.exe
proc5=TextInputHost.exe

[IgnoreNewWindowProcesses]
; New windows from these processes won't be repositioned under the cursor
proc1=zebar.exe
proc2=glazewm.exe
proc3=Telegram.exe
proc4=steam.exe
proc5=steamwebhelper.exe
proc6=wallpaper64.exe
proc7=obs64.exe
proc8=Discord.exe

[IgnoreClasses]
; Window classes ignored by focus-follow
class1=WorkerW
class2=Progman
class3=Shell_TrayWnd
class4=Shell_SecondaryTrayWnd
```

---

## Building from Source

Requires MinGW-w64 (via MSYS2) or MSVC.

**MinGW-w64 (MSYS2 MinGW64 shell):**
```
gcc -O2 -municode -mwindows main.c window_state.c util.c focus.c desktop.c hotkeys.c tray.c -o wm.exe -luser32 -lgdi32 -ldwmapi -ladvapi32 -lshell32 -lole32
```

**MSVC (x64 Native Tools Command Prompt):**
```
cl /O2 /DUNICODE /D_UNICODE main.c window_state.c util.c focus.c desktop.c hotkeys.c tray.c user32.lib gdi32.lib dwmapi.lib advapi32.lib shell32.lib ole32.lib /link /SUBSYSTEM:WINDOWS
```

---

## Credits

- [VirtualDesktopAccessor](https://github.com/Ciantic/VirtualDesktopAccessor) by Ciantic — virtual desktop switching on Win10/11
- [AltSnap](https://github.com/RamonUnch/AltSnap) by RamonUnch — recommended companion for drag-to-resize
