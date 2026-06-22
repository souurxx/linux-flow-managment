# wm installer — linux-flow-managment
# Run as Administrator:
#   irm https://raw.githubusercontent.com/souurxx/linux-flow-managment/main/wmsetup.ps1 | iex

$ErrorActionPreference = "Stop"

$repo    = "https://raw.githubusercontent.com/souurxx/linux-flow-managment/main"
$installDir = "$env:USERPROFILE\Documents\linux-flow-mangment"

Write-Host ""
Write-Host "  wm — window manager installer" -ForegroundColor Cyan
Write-Host "  Installing to: $installDir" -ForegroundColor Gray
Write-Host ""

# ── Create install folder ────────────────────────────────────────────────
if (-not (Test-Path $installDir)) {
    New-Item -ItemType Directory -Path $installDir | Out-Null
}

# ── Detect Windows version and pick the right DLL ───────────────────────
$build = [System.Environment]::OSVersion.Version.Build
if ($build -ge 22000) {
    $dllSrc  = "$repo/VirtualDesktopAccessorwin11.dll"
    $winVer  = "Windows 11"
} else {
    $dllSrc  = "$repo/VirtualDesktopAccessor.dll"
    $winVer  = "Windows 10"
}

Write-Host "  Detected: $winVer (build $build)" -ForegroundColor Gray

# ── Download files ───────────────────────────────────────────────────────
$files = @(
    @{ Url = "$repo/queen.exe";  Dest = "$installDir\queen.exe";                  Label = "queen.exe" },
    @{ Url = $dllSrc;            Dest = "$installDir\VirtualDesktopAccessor.dll"; Label = "VirtualDesktopAccessor.dll" },
    @{ Url = "$repo/wm.ico";     Dest = "$installDir\wm.ico";                     Label = "wm.ico" }
)

foreach ($f in $files) {
    Write-Host "  Downloading $($f.Label)..." -ForegroundColor Yellow
    try {
        Invoke-WebRequest -Uri $f.Url -OutFile $f.Dest -UseBasicParsing
        Write-Host "  OK" -ForegroundColor Green
    } catch {
        Write-Host "  FAILED: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host "  Download it manually from: $($f.Url)" -ForegroundColor Gray
    }
}

# ── Add to startup (current user, no UAC prompt on login) ───────────────
$startupDir = [System.Environment]::GetFolderPath("Startup")
$shortcutPath = "$startupDir\wm.lnk"

Write-Host ""
Write-Host "  Adding to startup..." -ForegroundColor Yellow

try {
    $shell    = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath       = "$installDir\queen.exe"
    $shortcut.WorkingDirectory = $installDir
    $shortcut.Description      = "wm window manager"
    $shortcut.Save()
    Write-Host "  Startup shortcut created" -ForegroundColor Green
} catch {
    Write-Host "  Could not create startup shortcut: $($_.Exception.Message)" -ForegroundColor Red
}

# ── Launch ───────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "  Launching queen.exe..." -ForegroundColor Yellow

try {
    Start-Process -FilePath "$installDir\queen.exe" -Verb RunAs
    Write-Host "  Launched (UAC prompt may appear)" -ForegroundColor Green
} catch {
    Write-Host "  Could not launch automatically. Run $installDir\queen.exe manually." -ForegroundColor Red
}

Write-Host ""
Write-Host "  Done. queen.exe will launch automatically on next login." -ForegroundColor Cyan
Write-Host "  Install folder: $installDir" -ForegroundColor Gray
Write-Host ""
