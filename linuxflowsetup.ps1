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
Write-Host "   linux-flow-mangment Setup" -ForegroundColor Cyan
Write-Host "═══════════════════════════════════════════" -ForegroundColor Cyan
Write-Host ""

# ── 1. Download master.exe ────────────────────────────────────────────────────
Write-Host "[1/3] Downloading master.exe..." -ForegroundColor Yellow
curl.exe -L "https://github.com/souurxx/linux-flow-mangment/raw/main/master.exe" -o "$installDir\master.exe"
Write-Host "      master.exe saved." -ForegroundColor Green

# ── 2. Download VirtualDesktopAccessor.dll ────────────────────────────────────
Write-Host "[2/3] Downloading VirtualDesktopAccessor.dll..." -ForegroundColor Yellow
$winBuild = [System.Environment]::OSVersion.Version.Build
if ($winBuild -lt 22000) {
    # Windows 10
    curl.exe -L "https://github.com/souurxx/linux-flow-mangment/raw/main/VirtualDesktopAccessor.dll" -o "$installDir\VirtualDesktopAccessor.dll"
} else {
    # Windows 11
    curl.exe -L "https://github.com/Ciantic/VirtualDesktopAccessor/releases/latest/download/VirtualDesktopAccessor.dll" -o "$installDir\VirtualDesktopAccessor.dll"
}
Write-Host "      DLL saved." -ForegroundColor Green

# ── 3. Install AltSnap ────────────────────────────────────────────────────────
Write-Host "[3/3] Downloading and installing AltSnap..." -ForegroundColor Yellow
curl.exe -L "https://github.com/RamonUnch/AltSnap/releases/download/1.67/AltSnap1.67-x64-inst.exe" -o "$tempDir\AltSnap_setup.exe"
Start-Process "$tempDir\AltSnap_setup.exe" -ArgumentList "/S" -Wait
Write-Host "      AltSnap installed." -ForegroundColor Green

# ── Add master.exe to startup ─────────────────────────────────────────────────
Write-Host "Setting up autostart..." -ForegroundColor Yellow
$WshShell = New-Object -ComObject WScript.Shell
$shortcut = $WshShell.CreateShortcut("$startupDir\linux-flow-mangment.lnk")
$shortcut.TargetPath = "$installDir\master.exe"
$shortcut.WorkingDirectory = $installDir
$shortcut.Save()
Write-Host "      Autostart set." -ForegroundColor Green

# ── Add AltSnap to startup ────────────────────────────────────────────────────
$altSnapExe = "$env:APPDATA\AltSnap\AltSnap.exe"
if (!(Test-Path $altSnapExe)) { $altSnapExe = "$env:ProgramFiles\AltSnap\AltSnap.exe" }
if (!(Test-Path $altSnapExe)) { $altSnapExe = "$env:LOCALAPPDATA\Programs\AltSnap\AltSnap.exe" }
if (Test-Path $altSnapExe) {
    $adShortcut = $WshShell.CreateShortcut("$startupDir\AltSnap.lnk")
    $adShortcut.TargetPath = $altSnapExe
    $adShortcut.Save()
    Write-Host "      AltSnap autostart set." -ForegroundColor Green
} else {
    Write-Host "      AltSnap.exe not found — add it to startup manually." -ForegroundColor Red
}

# ── Launch both now ───────────────────────────────────────────────────────────
Write-Host ""
Write-Host "Launching..." -ForegroundColor Cyan
if (Test-Path $altSnapExe) { Start-Process $altSnapExe }
Start-Process "$installDir\master.exe"

# ── Cleanup ───────────────────────────────────────────────────────────────────
Remove-Item $tempDir -Recurse -Force

Write-Host ""
Write-Host "═══════════════════════════════════════════" -ForegroundColor Green
Write-Host "   All done! Everything is running." -ForegroundColor Green
Write-Host "═══════════════════════════════════════════" -ForegroundColor Green
Write-Host ""