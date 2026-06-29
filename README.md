linux-flow-managment
Linux-style window management for Windows. Keyboard-driven focus, virtual desktops, snap, and spawn-under-mouse — all without leaving Windows.
Features

Focus follows mouse — hover over a window to focus it, no click needed
Spawn under mouse — new windows appear centered on your cursor
Virtual desktops — instant switch with Alt+1/2/3, move windows silently
Snap — half-screen left/right on any monitor
Maximize/restore — single hotkey toggle

Requirements

Windows 10 or 11
Run as Administrator (the exe will prompt automatically)


IMPORTANT: Create virtual desktops yourself first by pressing Win + Tab then clicking "New Desktop". The script supports up to 4. You can't switch between desktops until you create them.

One-click Install
Open PowerShell as Administrator and run:
irm https://github.com/souurxx/linux-flow-managment/raw/main/linuxflowsetup.ps1 | iex
This will:

Download klien.exe and the correct VirtualDesktopAccessor.dll for your Windows version
Install AltSnap (for easy window dragging and resizing)
Set both to launch on startup
Launch everything immediately

Manual Install

Download klien.exe from this repo
Windows 10: download VirtualDesktopAccessor.dll from this repo
Windows 11: download VirtualDesktopAccessor_Win11.dll from this repo and rename it to VirtualDesktopAccessor.dll
Put both files in the same folder
Run klien.exe

Hotkeys
HotkeyActionAlt+1 / 2 / 3Switch to desktop 1 / 2 / 3Alt+4Move window to desktop 4 and switchAlt+AMove window to desktop 1 (stay put)Alt+DMove window to desktop 2 (stay put)Alt+FMove window to desktop 3 (stay put)Alt+WMaximize → Fake fullscreen → Restore (cycles)Alt+QSnap leftAlt+ESnap rightAlt+ZClose windowAlt+CMinimize windowAlt+SOpen FirefoxAlt+XOpen File ExplorerAlt+NOpen NotepadAlt+EnterOpen TerminalAlt+SpaceOpen Brave (new window)Ctrl+SpaceNew tab (Firefox / Brave)Ctrl+FClose tab (Firefox / Brave)
Credits

VirtualDesktopAccessor by Ciantic
AltSnap by RamonUnch
Built with AutoHotkey v2
