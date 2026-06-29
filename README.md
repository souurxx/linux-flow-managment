# linux-flow-managment

Linux-style window management for Windows. Keyboard-driven focus, virtual desktops, snap, and spawn-under-mouse — all without leaving Windows.

# Features

- *Focus follows mouse* — hover over a window to focus it, no click needed
- *Spawn under mouse* — new windows appear centered on your cursor
- *Virtual desktops* — instant switch with Alt+1/2/3, move windows silently
- *Snap* — half-screen left/right on any monitor
- *Maximize/restore* — single hotkey toggle

# Requirements

- Windows 10 or 11
- Run as Administrator (the exe will prompt automatically)

> **IMPORTANT:** Create virtual desktops yourself first by pressing `Win + Tab` then clicking "New Desktop". The script supports up to 4. You can't switch between desktops until you create them.

## One-click Install

Open PowerShell as Administrator and run:

```
irm https://github.com/souurxx/linux-flow-managment/raw/main/linuxflowsetup.ps1 | iex
```

This will:

- Download `klien.exe` and the correct `VirtualDesktopAccessor.dll` for your Windows version
- Install AltSnap (for easy window dragging and resizing)
- Set both to launch on startup
- Launch everything immediately

# Manual Install

1. Download `klien.exe` from this repo
2. **Windows 10:** download `VirtualDesktopAccessor.dll` from this repo
3. **Windows 11:** download `VirtualDesktopAccessor_Win11.dll` from this repo and rename it to `VirtualDesktopAccessor.dll`
4. Put both files in the same folder
5. Run `klien.exe`

## Hotkeys

| Hotkey | Action |
|---|---|
| Alt+1 / 2 / 3 | Switch to desktop 1 / 2 / 3 |
| Alt+4 | Move window to desktop 4 and switch |
| Alt+A | Move window to desktop 1 (stay put) |
| Alt+D | Move window to desktop 2 (stay put) |
| Alt+F | Move window to desktop 3 (stay put) |
| Alt+W | Maximize → Fake fullscreen → Restore (cycles) |
| Alt+Q | Snap left |
| Alt+E | Snap right |
| Alt+Z | Close window |
| Alt+C | Minimize window |
| Alt+S | Open Firefox |
| Alt+X | Open File Explorer |
| Alt+N | Open Notepad |
| Alt+Enter | Open Terminal |
| Alt+Space | Open Brave (new window) |
| Ctrl+Space | New tab (Firefox / Brave) |
| Ctrl+F | Close tab (Firefox / Brave) |

## Credits

- [VirtualDesktopAccessor](https://github.com/Ciantic/VirtualDesktopAccessor) by Ciantic
- [AltSnap](https://github.com/RamonUnch/AltSnap) by RamonUnch
- Built with [AutoHotkey v2](https://www.autohotkey.com/)
