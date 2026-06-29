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

## Install Script

<details>
<summary>linuxflowsetup.ps1</summary>

```powershell
# ── setup.ps1 ─────────────────────────────────────────────────────────────────
# Downloads and sets up linux-flow-mangment + AltSnap
# Run as Administrator
# ─────────────────────────────────────────────────────────────────────────────

$ErrorActionPreference = "Stop"

$installDir = "$env:USERPROFILE\Documents\linux-flow-mangment"
$startupDir = "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup"
$tempDir    = "$env:TEMP\lfm_setup"

New-Item -ItemType Directory -Force -Path $installDir | Out-Null
New-Item -ItemType Directory -Force -Path $tempDir    | Out-Null

Write-Host ""
Write-Host "═══════════════════════════════════════════" -ForegroundColor Cyan
Write-Host " linux-flow-mangment Setup"                  -ForegroundColor Cyan
Write-Host "═══════════════════════════════════════════" -ForegroundColor Cyan
Write-Host ""

# ── 1. Download klien.exe ─────────────────────────────────────────────────────
Write-Host "[1/3] Downloading klien.exe..." -ForegroundColor Yellow
curl.exe -L "https://github.com/souurxx/linux-flow-mangment/raw/main/klien.exe" -o "$installDir\klien.exe"
Write-Host " klien.exe saved." -ForegroundColor Green

# ── 2. Download VirtualDesktopAccessor.dll ────────────────────────────────────
Write-Host "[2/3] Downloading VirtualDesktopAccessor.dll..." -ForegroundColor Yellow
$winBuild = [System.Environment]::OSVersion.Version.Build
if ($winBuild -lt 22000) {
    # Windows 10
    curl.exe -L "https://github.com/souurxx/linux-flow-mangment/raw/main/VirtualDesktopAccessor.dll" -o "$installDir\VirtualDesktopAccessor.dll"
} else {
    # Windows 11
    curl.exe -L "https://github.com/souurxx/linux-flow-mangment/raw/main/VirtualDesktopAccessor_Win11.dll" -o "$installDir\VirtualDesktopAccessor.dll"
}
Write-Host " DLL saved." -ForegroundColor Green

# ── 3. Install AltSnap ────────────────────────────────────────────────────────
Write-Host "[3/3] Downloading and installing AltSnap..." -ForegroundColor Yellow
curl.exe -L "https://github.com/RamonUnch/AltSnap/releases/download/1.67/AltSnap1.67-x64-inst.exe" -o "$tempDir\AltSnap_setup.exe"
Start-Process "$tempDir\AltSnap_setup.exe" -ArgumentList "/S" -Wait
Write-Host " AltSnap installed." -ForegroundColor Green

# ── Add klien.exe to startup ──────────────────────────────────────────────────
Write-Host "Setting up autostart..." -ForegroundColor Yellow
$WshShell = New-Object -ComObject WScript.Shell
$shortcut = $WshShell.CreateShortcut("$startupDir\linux-flow-mangment.lnk")
$shortcut.TargetPath       = "$installDir\klien.exe"
$shortcut.WorkingDirectory = $installDir
$shortcut.Save()
Write-Host " Autostart set." -ForegroundColor Green

# ── Add AltSnap to startup ────────────────────────────────────────────────────
$altSnapExe = "$env:APPDATA\AltSnap\AltSnap.exe"
if (!(Test-Path $altSnapExe)) { $altSnapExe = "$env:ProgramFiles\AltSnap\AltSnap.exe" }
if (!(Test-Path $altSnapExe)) { $altSnapExe = "$env:LOCALAPPDATA\Programs\AltSnap\AltSnap.exe" }

if (Test-Path $altSnapExe) {
    $adShortcut = $WshShell.CreateShortcut("$startupDir\AltSnap.lnk")
    $adShortcut.TargetPath = $altSnapExe
    $adShortcut.Save()
    Write-Host " AltSnap autostart set." -ForegroundColor Green
} else {
    Write-Host " AltSnap.exe not found — add it to startup manually." -ForegroundColor Red
}

# ── Launch both now ───────────────────────────────────────────────────────────
Write-Host ""
Write-Host "Launching..." -ForegroundColor Cyan
if (Test-Path $altSnapExe) { Start-Process $altSnapExe }
Start-Process "$installDir\klien.exe"

# ── Cleanup ───────────────────────────────────────────────────────────────────
Remove-Item $tempDir -Recurse -Force

Write-Host ""
Write-Host "═══════════════════════════════════════════" -ForegroundColor Green
Write-Host " All done! Everything is running."          -ForegroundColor Green
Write-Host "═══════════════════════════════════════════" -ForegroundColor Green
Write-Host ""
```

</details>

## Credits

- [VirtualDesktopAccessor](https://github.com/Ciantic/VirtualDesktopAccessor) by Ciantic
- [AltSnap](https://github.com/RamonUnch/AltSnap) by RamonUnch
- Built with [AutoHotkey v2](https://www.autohotkey.com/)
