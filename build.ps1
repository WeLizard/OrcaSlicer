# OrcaSlicer FilamentHub Edition - Build Menu
# Интерактивное меню для сборки и релиза

$ErrorActionPreference = "Stop"
$OrcaDir = $PSScriptRoot
$BuildDir = Join-Path $OrcaDir "build"
$DepsDir = Join-Path $OrcaDir "deps"
$DepsBuildDir = Join-Path $DepsDir "build"
$VersionIncFile = Join-Path $OrcaDir "version.inc"

# ============================================================
# Version Management (single source of truth: version.inc)
# ============================================================

function Get-StoredVersion {
    # Read raw SoftFever_VERSION from version.inc (e.g. "2.3.2-dev+fh")
    if (Test-Path $VersionIncFile) {
        $content = Get-Content $VersionIncFile -Raw
        if ($content -match 'set\(SoftFever_VERSION\s+"([^"]+)"\)') {
            return $Matches[1]
        }
    }
    return "0.0.0"
}

function Normalize-FHVersion {
    param([string]$VersionString)

    if (-not $VersionString) {
        return "0.0.0+fh"
    }

    $normalized = $VersionString.Trim()
    # Legacy migration: 2.3.2-dev-fh -> 2.3.2-dev+fh
    $normalized = $normalized -replace '-fh$', ''
    # Keep FilamentHub marker as semver build metadata so upstream stable releases
    # are not always considered newer than our same-numbered builds.
    $normalized = $normalized -replace '\+.*$', ''

    return "$normalized+fh"
}

function Get-BaseVersion {
    # Return the editable base version without FilamentHub metadata.
    $stored = Get-StoredVersion
    if (-not $stored) {
        return "0.0.0"
    }

    $base = $stored.Trim()
    $base = $base -replace '-fh$', ''
    $base = $base -replace '\+.*$', ''
    return $base
}

function Get-FHVersion {
    return Normalize-FHVersion (Get-StoredVersion)
}

function Set-BaseVersion {
    param([string]$NewVersion)
    # Write back to version.inc, always with +fh build metadata in SoftFever_VERSION.
    $normalizedVersion = Normalize-FHVersion $NewVersion
    $content = Get-Content $VersionIncFile -Raw
    $content = $content -replace 'set\(SoftFever_VERSION\s+"[^"]+"\)', "set(SoftFever_VERSION `"$normalizedVersion`")"
    $content | Set-Content $VersionIncFile -NoNewline -Encoding utf8
}

$Version = Get-FHVersion

function Get-UpstreamVersionFromBranch {
    param([string]$Branch)
    try {
        $content = (& git show "${Branch}:version.inc" 2>$null) | Out-String
        if ($LASTEXITCODE -eq 0 -and $content -match 'set\(SoftFever_VERSION\s+"([^"]+)"\)') {
            return $Matches[1]
        }
    } catch {}
    return $null
}

function Get-UpstreamInfo {
    # Returns hashtable with main (dev) and latest release versions
    $info = @{ Main = "unknown"; Release = $null; ReleaseBranch = $null }
    $main = Get-UpstreamVersionFromBranch "upstream/main"
    if ($main) { $info.Main = $main }
    # Check top release branches and pick the one with highest version in version.inc
    $relBranches = git branch -r --list "upstream/release/v*" --sort=-version:refname 2>$null |
        ForEach-Object { $_.Trim() } |
        Select-Object -First 3
    foreach ($branch in $relBranches) {
        $rel = Get-UpstreamVersionFromBranch $branch
        if ($rel -and $rel -ne $main) {
            if (-not $info.Release -or ([version]($rel -replace '-.*','') -gt [version]($info.Release -replace '-.*',''))) {
                $info.Release = $rel
                $info.ReleaseBranch = $branch -replace '^upstream/', ''
            }
        }
    }
    return $info
}

# ============================================================
# FilamentHub source files (touch before build to force recompile)
# ============================================================

$FilamentHubFiles = @(
    "src/slic3r/GUI/FilamentHubPanel.cpp",
    "src/slic3r/GUI/FilamentHubPanel.hpp",
    "src/slic3r/Utils/FilamentHubClient.cpp",
    "src/slic3r/Utils/FilamentHubClient.hpp"
)

# ============================================================
# Helper Functions
# ============================================================

function Touch-FilamentHubFiles {
    Write-Host "  Touching FilamentHub files to force recompile..." -ForegroundColor DarkGray
    foreach ($file in $FilamentHubFiles) {
        $fullPath = Join-Path $OrcaDir $file
        if (Test-Path $fullPath) {
            (Get-Item $fullPath).LastWriteTime = Get-Date
        }
    }
}

function Clean-BuildCache {
    Write-Host "`n>>> Cleaning FilamentHub build cache..." -ForegroundColor Yellow

    $objPatterns = @(
        "build/src/slic3r/libslic3r_gui.dir/Release/FilamentHubClient.obj",
        "build/src/slic3r/libslic3r_gui.dir/Release/FilamentHubPanel.obj",
        "build/src/slic3r/libslic3r_gui.dir/Debug/FilamentHubClient.obj",
        "build/src/slic3r/libslic3r_gui.dir/Debug/FilamentHubPanel.obj"
    )

    $deleted = 0
    foreach ($obj in $objPatterns) {
        $fullPath = Join-Path $OrcaDir $obj
        if (Test-Path $fullPath) {
            Remove-Item $fullPath -Force
            $deleted++
        }
    }

    # Also try wildcard search for FilamentHub related .obj
    $buildDirs = @("build/src/slic3r/libslic3r_gui.dir/Release", "build/src/slic3r/libslic3r_gui.dir/Debug")
    foreach ($dir in $buildDirs) {
        $fullDir = Join-Path $OrcaDir $dir
        if (Test-Path $fullDir) {
            Get-ChildItem -Path $fullDir -Filter "*FilamentHub*" -ErrorAction SilentlyContinue | ForEach-Object {
                Remove-Item $_.FullName -Force
                $deleted++
            }
        }
    }

    if ($deleted -gt 0) {
        Write-Host "  Deleted $deleted cached .obj files" -ForegroundColor Green
    } else {
        Write-Host "  No cached files found" -ForegroundColor DarkGray
    }
}

function Clean-FullBuild {
    Write-Host "`n>>> Full Clean Build..." -ForegroundColor Yellow
    Write-Host "  This will delete the entire build folder!" -ForegroundColor Red
    Write-Host "  (deps will be preserved)" -ForegroundColor DarkGray
    Write-Host ""

    $confirm = Read-Host "Are you sure? (yes/no)"
    if ($confirm -ne "yes") {
        Write-Host "  Cancelled" -ForegroundColor DarkGray
        return
    }

    if (Test-Path $BuildDir) {
        Remove-Item $BuildDir -Recurse -Force
        Write-Host "  [OK] Build folder deleted" -ForegroundColor Green
    } else {
        Write-Host "  Build folder doesn't exist" -ForegroundColor DarkGray
    }

    # Also clean VS cache
    $vsDir = Join-Path $OrcaDir ".vs"
    if (Test-Path $vsDir) {
        Remove-Item $vsDir -Recurse -Force
        Write-Host "  [OK] .vs folder deleted" -ForegroundColor Green
    }
}

function Clean-Everything {
    Write-Host "`n>>> NUCLEAR CLEAN - Delete build + deps..." -ForegroundColor Red
    Write-Host "  This will delete EVERYTHING including deps!" -ForegroundColor Red
    Write-Host "  Next build will take 30-60 minutes for deps" -ForegroundColor Yellow
    Write-Host ""

    $confirm = Read-Host "Type 'DELETE ALL' to confirm"
    if ($confirm -ne "DELETE ALL") {
        Write-Host "  Cancelled" -ForegroundColor DarkGray
        return
    }

    if (Test-Path $BuildDir) {
        Remove-Item $BuildDir -Recurse -Force
        Write-Host "  [OK] Build folder deleted" -ForegroundColor Green
    }

    $depsBuild = Join-Path $DepsDir "build"
    if (Test-Path $depsBuild) {
        Remove-Item $depsBuild -Recurse -Force
        Write-Host "  [OK] Deps build folder deleted" -ForegroundColor Green
    }

    $vsDir = Join-Path $OrcaDir ".vs"
    if (Test-Path $vsDir) {
        Remove-Item $vsDir -Recurse -Force
        Write-Host "  [OK] .vs folder deleted" -ForegroundColor Green
    }

    Write-Host "`n  Clean complete. Run [F] Full Build to rebuild from scratch." -ForegroundColor Yellow
}

# ============================================================
# Build Functions
# ============================================================

function Build-Deps {
    param(
        [string]$BuildType = "Release"
    )

    Write-Host "`n>>> Building Dependencies ($BuildType)..." -ForegroundColor Yellow
    Write-Host "  Uses original build_release_vs2022.bat deps" -ForegroundColor DarkYellow
    Write-Host "  This may take 30-60 minutes on first build..." -ForegroundColor DarkYellow
    Write-Host ""

    $confirm = Read-Host "Continue? (y/n)"
    if ($confirm -ne "y") {
        Write-Host "  [CANCELLED]" -ForegroundColor DarkGray
        return $false
    }

    Set-Location $OrcaDir

    # Call original builder for deps only
    $batArgs = "deps"
    if ($BuildType -eq "Debug") { $batArgs = "deps debug" }
    elseif ($BuildType -eq "RelWithDebInfo") { $batArgs = "deps debuginfo" }

    Write-Host "`n  Running: build_release_vs2022.bat $batArgs" -ForegroundColor Cyan
    & cmd /c "build_release_vs2022.bat $batArgs"

    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n  [ERROR] Deps build failed!" -ForegroundColor Red
        Set-Location $OrcaDir
        return $false
    }

    Write-Host "`n  [OK] Dependencies built successfully!" -ForegroundColor Green
    Set-Location $OrcaDir
    return $true
}

function Prompt-VersionBeforeBuild {
    $base = Get-BaseVersion
    Write-Host ""
    Write-Host "  Base version (version.inc): " -NoNewline
    Write-Host "$base" -ForegroundColor Cyan
    Write-Host "  Build version:              " -NoNewline
    Write-Host "$Version" -ForegroundColor Yellow
    $changeVer = Read-Host "  Change base version? (Enter = keep, or type new, e.g. 2.3.3-dev)"
    if ($changeVer -and $changeVer -ne "") {
        Set-BaseVersion $changeVer
        $script:Version = Get-FHVersion
        Write-Host "  version.inc -> $changeVer" -ForegroundColor Green
        Write-Host "  Build version -> $Version" -ForegroundColor Green
    }
}

function Show-PostBuildMenu {
    Write-Host ""
    Write-Host "  ============================================" -ForegroundColor Green
    Write-Host "  Build successful! What's next?" -ForegroundColor Green
    Write-Host "  ============================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "  [1] Create Installer (NSIS .exe)" -ForegroundColor White
    Write-Host "  [2] Create Portable ZIP" -ForegroundColor White
    Write-Host "  [3] Create BOTH (Installer + Portable)" -ForegroundColor White
    Write-Host "  [4] Run OrcaSlicer now" -ForegroundColor Cyan
    Write-Host "  [0] Back to main menu" -ForegroundColor DarkGray
    Write-Host ""

    $postChoice = Read-Host "  Select"
    switch ($postChoice) {
        "1" {
            Build-Installer
        }
        "2" {
            Create-PortableZip
        }
        "3" {
            Build-Installer
            Create-PortableZip
            Write-Host ""
            Write-Host "  All packages created for v$Version!" -ForegroundColor Green
        }
        "4" {
            $exe = Join-Path $BuildDir "OrcaSlicer/orca-slicer.exe"
            if (Test-Path $exe) {
                Write-Host "  Launching OrcaSlicer..." -ForegroundColor Cyan
                Start-Process $exe
            } else {
                Write-Host "  [ERROR] orca-slicer.exe not found" -ForegroundColor Red
            }
        }
        default {
            Write-Host "  Returning to main menu..." -ForegroundColor DarkGray
        }
    }
}

function Build-Slicer {
    param(
        [string]$BuildType = "Release",
        [switch]$SkipDepsCheck,
        [switch]$SkipPostBuild
    )

    Write-Host "`n>>> Building OrcaSlicer ($BuildType)..." -ForegroundColor Yellow

    # Offer to change version before build
    Prompt-VersionBeforeBuild

    # Check if deps exist
    $depsPath = Join-Path $OrcaDir "deps/build/OrcaSlicer_dep"
    if (-not $SkipDepsCheck -and -not (Test-Path $depsPath)) {
        Write-Host "  [WARNING] Dependencies not found!" -ForegroundColor Yellow
        Write-Host ""
        $buildDeps = Read-Host "Build deps first? This will take 30-60 min (y/n)"
        if ($buildDeps -eq "y") {
            $result = Build-Deps -BuildType $BuildType
            if (-not $result) { return $false }
        } else {
            Write-Host "  [CANCELLED] Cannot build slicer without deps" -ForegroundColor Red
            return $false
        }
    }

    Set-Location $OrcaDir

    # Always touch FilamentHub files to ensure they get recompiled
    Touch-FilamentHubFiles

    # Call original build_release_vs2022.bat for slicer build
    # It handles: cmake configure + build + gettext + install
    $batArgs = "slicer"
    if ($BuildType -eq "Debug") { $batArgs = "slicer debug" }
    elseif ($BuildType -eq "RelWithDebInfo") { $batArgs = "slicer debuginfo" }

    Write-Host "`n  [1/2] Running original builder: build_release_vs2022.bat $batArgs" -ForegroundColor Cyan
    & cmd /c "build_release_vs2022.bat $batArgs"

    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n  [ERROR] Build failed!" -ForegroundColor Red
        Set-Location $OrcaDir
        return $false
    }

    # Determine build output directory name
    $buildDirName = "build"
    if ($BuildType -eq "Debug") { $buildDirName = "build-dbg" }
    elseif ($BuildType -eq "RelWithDebInfo") { $buildDirName = "build-dbginfo" }
    $actualBuildDir = Join-Path $OrcaDir $buildDirName

    # Copy runtime DLLs to install directory (original .bat doesn't do this)
    Write-Host "`n  [2/2] Copying runtime DLLs to install directory..." -ForegroundColor Cyan
    $installDir = Join-Path $actualBuildDir "OrcaSlicer"
    $srcBinDir = Join-Path $actualBuildDir "src\$BuildType"

    if ((Test-Path $installDir) -and (Test-Path $srcBinDir)) {
        $dllsCopied = 0
        Get-ChildItem -Path $srcBinDir -Filter "*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
            $destFile = Join-Path $installDir $_.Name
            if (-not (Test-Path $destFile)) {
                Copy-Item $_.FullName $installDir -Force
                $dllsCopied++
            }
        }

        # Also copy resources if missing in install dir
        $srcResources = Join-Path $srcBinDir "resources"
        $destResources = Join-Path $installDir "resources"
        if ((Test-Path $srcResources) -and -not (Test-Path $destResources)) {
            Copy-Item $srcResources $destResources -Recurse -Force
            Write-Host "    Copied resources directory" -ForegroundColor DarkGray
        }

        if ($dllsCopied -gt 0) {
            Write-Host "    Copied $dllsCopied DLL(s) to install directory" -ForegroundColor Green
        } else {
            Write-Host "    All DLLs already present" -ForegroundColor DarkGray
        }
    } else {
        Write-Host "    [WARNING] Could not find directories:" -ForegroundColor Yellow
        if (-not (Test-Path $srcBinDir)) {
            Write-Host "    Missing: $srcBinDir" -ForegroundColor DarkGray
        }
        if (-not (Test-Path $installDir)) {
            Write-Host "    Missing: $installDir" -ForegroundColor DarkGray
        }
    }

    Write-Host "`n  [OK] Slicer built successfully!" -ForegroundColor Green
    Write-Host "  Run: $installDir\orca-slicer.exe" -ForegroundColor Cyan
    Set-Location $OrcaDir

    # Show post-build packaging menu (unless called from Build-AllWindows)
    if (-not $SkipPostBuild -and $BuildType -eq "Release") {
        Show-PostBuildMenu
    }

    return $true
}

function Build-Debug {
    Write-Host "`n>>> Building Debug..." -ForegroundColor Yellow
    Build-Slicer -BuildType "Debug"
}

function Build-RelWithDebInfo {
    Write-Host "`n>>> Building RelWithDebInfo..." -ForegroundColor Yellow
    Build-Slicer -BuildType "RelWithDebInfo"
}

function Build-Installer {
    Write-Host "`n>>> Building NSIS Installer..." -ForegroundColor Yellow

    if (-not (Test-Path $BuildDir)) {
        Write-Host "  [ERROR] Build folder not found! Build slicer first." -ForegroundColor Red
        return
    }

    Set-Location $BuildDir

    & cpack -G NSIS -C Release
    if ($LASTEXITCODE -eq 0) {
        $oldName = Get-ChildItem -Path $BuildDir -Filter "OrcaSlicer_Windows_Installer_*.exe" |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1

        if ($oldName) {
            $newName = "OrcaSlicer-FilamentHub-$Version-win64-setup.exe"
            $newPath = Join-Path $OrcaDir $newName
            Copy-Item $oldName.FullName $newPath -Force
            Write-Host "`n  [OK] Installer created: $newName" -ForegroundColor Green
            Write-Host "  Size: $([math]::Round((Get-Item $newPath).Length / 1MB, 1)) MB" -ForegroundColor DarkGray
        }
    } else {
        Write-Host "`n  [ERROR] Installer build failed!" -ForegroundColor Red
    }

    Set-Location $OrcaDir
}

function Create-PortableZip {
    Write-Host "`n>>> Creating Portable ZIP..." -ForegroundColor Yellow

    $sourcePath = Join-Path $BuildDir "OrcaSlicer/*"
    if (-not (Test-Path (Join-Path $BuildDir "OrcaSlicer"))) {
        Write-Host "  [ERROR] Build output not found! Build slicer first." -ForegroundColor Red
        return
    }

    $zipName = "OrcaSlicer-FilamentHub-$Version-win64-portable.zip"
    $zipPath = Join-Path $OrcaDir $zipName

    if (Test-Path $zipPath) {
        Remove-Item $zipPath -Force
    }

    Compress-Archive -Path $sourcePath -DestinationPath $zipPath -Force

    if (Test-Path $zipPath) {
        Write-Host "`n  [OK] Portable ZIP created: $zipName" -ForegroundColor Green
        Write-Host "  Size: $([math]::Round((Get-Item $zipPath).Length / 1MB, 1)) MB" -ForegroundColor DarkGray
    } else {
        Write-Host "`n  [ERROR] ZIP creation failed!" -ForegroundColor Red
    }
}

# ============================================================
# Full Builds
# ============================================================

function Build-FullWindows {
    Write-Host "`n>>> Full Build Windows (deps + slicer)..." -ForegroundColor Yellow
    Write-Host "  Uses original build_release_vs2022.bat (no args = deps + slicer)" -ForegroundColor DarkGray

    # Offer to change version before build
    Prompt-VersionBeforeBuild

    Write-Host ""
    $confirm = Read-Host "Continue? This will take 30-60 min (y/n)"
    if ($confirm -ne "y") {
        Write-Host "  [CANCELLED]" -ForegroundColor DarkGray
        return
    }

    Set-Location $OrcaDir

    # Touch FilamentHub files to force recompile
    Touch-FilamentHubFiles

    # Call original builder without args = builds deps + slicer + install
    Write-Host "`n  Running: build_release_vs2022.bat (full build)" -ForegroundColor Cyan
    & cmd /c "build_release_vs2022.bat"

    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n  [ERROR] Full build failed!" -ForegroundColor Red
        Set-Location $OrcaDir
        return
    }

    # Copy DLLs to install directory
    $installDir = Join-Path $BuildDir "OrcaSlicer"
    $srcBinDir = Join-Path $BuildDir "src\Release"

    if ((Test-Path $installDir) -and (Test-Path $srcBinDir)) {
        Write-Host "`n  Copying runtime DLLs to install directory..." -ForegroundColor Cyan
        $dllsCopied = 0
        Get-ChildItem -Path $srcBinDir -Filter "*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
            $destFile = Join-Path $installDir $_.Name
            if (-not (Test-Path $destFile)) {
                Copy-Item $_.FullName $installDir -Force
                $dllsCopied++
            }
        }
        if ($dllsCopied -gt 0) {
            Write-Host "    Copied $dllsCopied DLL(s)" -ForegroundColor Green
        }
    }

    Write-Host "`n  [OK] Full build complete!" -ForegroundColor Green
    Write-Host "  Run: $installDir\orca-slicer.exe" -ForegroundColor Cyan
    Set-Location $OrcaDir

    # Post-build packaging menu
    Show-PostBuildMenu
}

function Build-AllWindows {
    Write-Host "`n>>> Building ALL Windows packages..." -ForegroundColor Yellow
    Write-Host "  Slicer + Installer + Portable ZIP" -ForegroundColor DarkGray

    # Offer to change version before build
    Prompt-VersionBeforeBuild

    Write-Host ""

    # Check deps first
    $depsPath = Join-Path $OrcaDir "deps/build/OrcaSlicer_dep"
    if (-not (Test-Path $depsPath)) {
        Write-Host "  [ERROR] Dependencies not found!" -ForegroundColor Red
        Write-Host "  Run [F] Full Build Windows first, or [D] Build Dependencies" -ForegroundColor Yellow
        return
    }

    # Build slicer
    Write-Host "=== Step 1/3: Building Slicer ===" -ForegroundColor Cyan
    $result = Build-Slicer -BuildType "Release" -SkipDepsCheck -SkipPostBuild
    if (-not $result) {
        Write-Host "`n  [ERROR] Slicer build failed! Cannot continue." -ForegroundColor Red
        return
    }

    # Build Installer
    Write-Host "`n=== Step 2/3: Building Installer ===" -ForegroundColor Cyan
    Build-Installer

    # Create Portable ZIP
    Write-Host "`n=== Step 3/3: Creating Portable ZIP ===" -ForegroundColor Cyan
    Create-PortableZip

    Write-Host "`n  [OK] ALL Windows packages complete!" -ForegroundColor Green
}

function Create-LinuxPortableTar {
    Write-Host "`n>>> Creating Linux Portable tar.gz..." -ForegroundColor Yellow

    $appImage = Get-ChildItem -Path $OrcaDir -Filter "OrcaSlicer-FilamentHub-$Version-linux-x64.AppImage" -ErrorAction SilentlyContinue |
        Select-Object -First 1

    if (-not $appImage) {
        Write-Host "  [ERROR] AppImage not found for v$Version! Build Linux first." -ForegroundColor Red
        return
    }

    $tarName = "OrcaSlicer-FilamentHub-$Version-linux-x64-portable.tar.gz"
    $tarPath = Join-Path $OrcaDir $tarName

    # Create a temp staging directory with the AppImage + a launcher script
    $stagingDir = Join-Path $OrcaDir ".linux_portable_staging"
    $innerDir = Join-Path $stagingDir "OrcaSlicer-FilamentHub"
    if (Test-Path $stagingDir) { Remove-Item $stagingDir -Recurse -Force }
    New-Item -Path $innerDir -ItemType Directory -Force | Out-Null

    Copy-Item $appImage.FullName (Join-Path $innerDir $appImage.Name) -Force

    # Create a launcher script
    $launcherContent = @"
#!/bin/bash
DIR=`$(cd "`$(dirname "`$0")" && pwd)
chmod +x "`$DIR/$($appImage.Name)"
exec "`$DIR/$($appImage.Name)" "`$@"
"@
    $launcherContent | Out-File (Join-Path $innerDir "run.sh") -Encoding utf8 -NoNewline

    # Use tar via docker or wsl if available, otherwise just zip
    $wslAvailable = $false
    try {
        $wslTest = wsl echo ok 2>&1
        if ($wslTest -eq "ok") { $wslAvailable = $true }
    } catch {}

    if ($wslAvailable) {
        $wslStaging = wsl wslpath -u ($stagingDir -replace '\\','/')
        wsl tar -czf (wsl wslpath -u ($tarPath -replace '\\','/')) -C $wslStaging "OrcaSlicer-FilamentHub"
    } else {
        # Fallback: create .zip instead of .tar.gz on pure Windows
        $tarName = "OrcaSlicer-FilamentHub-$Version-linux-x64-portable.zip"
        $tarPath = Join-Path $OrcaDir $tarName
        if (Test-Path $tarPath) { Remove-Item $tarPath -Force }
        Compress-Archive -Path "$innerDir/*" -DestinationPath $tarPath -Force
    }

    Remove-Item $stagingDir -Recurse -Force -ErrorAction SilentlyContinue

    if (Test-Path $tarPath) {
        Write-Host "`n  [OK] Linux portable created: $tarName" -ForegroundColor Green
        Write-Host "  Size: $([math]::Round((Get-Item $tarPath).Length / 1MB, 1)) MB" -ForegroundColor DarkGray
    } else {
        Write-Host "`n  [ERROR] Failed to create portable archive" -ForegroundColor Red
    }
}

function Show-PostBuildMenuLinux {
    Write-Host ""
    Write-Host "  ============================================" -ForegroundColor Green
    Write-Host "  Linux build successful! What's next?" -ForegroundColor Green
    Write-Host "  ============================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "  [1] Create Linux Portable archive" -ForegroundColor White
    Write-Host "  [2] Copy to Server (Samba)" -ForegroundColor White
    Write-Host "  [3] Both (Portable + Copy to Server)" -ForegroundColor White
    Write-Host "  [0] Back to main menu" -ForegroundColor DarkGray
    Write-Host ""

    $postChoice = Read-Host "  Select"
    switch ($postChoice) {
        "1" {
            Create-LinuxPortableTar
        }
        "2" {
            Copy-ToServer
        }
        "3" {
            Create-LinuxPortableTar
            Copy-ToServer
            Write-Host ""
            Write-Host "  All Linux packages done for v$Version!" -ForegroundColor Green
        }
        default {
            Write-Host "  Returning to main menu..." -ForegroundColor DarkGray
        }
    }
}

function Build-FullLinux {
    Write-Host "`n>>> Full Build Linux (Docker)..." -ForegroundColor Yellow
    Write-Host "  This will build deps + slicer + AppImage in Docker container" -ForegroundColor DarkGray
    Write-Host "  Expected time: 30-60 minutes (first build)" -ForegroundColor DarkYellow

    # Offer to change version before build
    Prompt-VersionBeforeBuild

    Write-Host ""
    Set-Location $OrcaDir

    # Check if Docker is running
    Write-Host "  Checking Docker..." -ForegroundColor DarkGray
    $dockerStatus = docker info 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`n  [ERROR] Docker is not running!" -ForegroundColor Red
        Write-Host "  Start Docker Desktop first, then try again" -ForegroundColor Yellow
        return
    }
    Write-Host "  [OK] Docker is running" -ForegroundColor Green
    Write-Host ""

    $confirm = Read-Host "Continue with Linux build? (y/n)"
    if ($confirm -ne "y") {
        Write-Host "  [CANCELLED]" -ForegroundColor DarkGray
        return
    }

    Write-Host "`n  Building in Docker (this will take a while)..." -ForegroundColor Cyan
    & docker build -t orcaslicer-fh-build -f scripts/Dockerfile .

    if ($LASTEXITCODE -eq 0) {
        Write-Host "`n  Extracting AppImage from container..." -ForegroundColor Cyan
        $containerId = docker create orcaslicer-fh-build
        $dockerOutputDir = Join-Path $OrcaDir "docker_output"
        if (Test-Path $dockerOutputDir) {
            Remove-Item $dockerOutputDir -Recurse -Force -ErrorAction SilentlyContinue
        }
        New-Item -Path $dockerOutputDir -ItemType Directory -Force | Out-Null

        $appImagePath = (& docker run --rm --entrypoint sh orcaslicer-fh-build -lc "find /OrcaSlicer/build -maxdepth 2 -name '*.AppImage' -print -quit").Trim()
        if ($appImagePath) {
            & docker cp "${containerId}:${appImagePath}" "$dockerOutputDir/"
        }
        & docker rm $containerId | Out-Null

        $appImage = Get-ChildItem -Path $dockerOutputDir -Filter "*.AppImage" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1

        if ($appImage) {
            $newName = "OrcaSlicer-FilamentHub-$Version-linux-x64.AppImage"
            Move-Item $appImage.FullName (Join-Path $OrcaDir $newName) -Force
            Remove-Item $dockerOutputDir -Recurse -Force -ErrorAction SilentlyContinue
            Write-Host "`n  [OK] Linux AppImage created: $newName" -ForegroundColor Green
            Write-Host "  Size: $([math]::Round((Get-Item (Join-Path $OrcaDir $newName)).Length / 1MB, 1)) MB" -ForegroundColor DarkGray

            # Post-build menu for Linux
            Show-PostBuildMenuLinux
        } else {
            Write-Host "`n  [WARNING] AppImage not found in Docker output" -ForegroundColor Yellow
        }
    } else {
        Write-Host "`n  [ERROR] Docker build failed!" -ForegroundColor Red
    }

    Set-Location $OrcaDir
}

function Package-ExistingBuild {
    Write-Host "`n>>> Package existing build (v$Version)" -ForegroundColor Yellow

    # Check if Windows build exists
    $winBuild = Join-Path $BuildDir "OrcaSlicer/orca-slicer.exe"
    $linuxBuild = Get-ChildItem -Path $OrcaDir -Filter "OrcaSlicer-FilamentHub-*-linux-x64.AppImage" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1

    $hasWin = Test-Path $winBuild
    $hasLinux = $null -ne $linuxBuild

    if (-not $hasWin -and -not $hasLinux) {
        Write-Host "  [ERROR] No builds found! Build slicer first." -ForegroundColor Red
        return
    }

    Write-Host ""
    Write-Host "  Detected builds:" -ForegroundColor Cyan
    if ($hasWin) {
        $winInfo = Get-Item $winBuild
        Write-Host "  [W] Windows build  " -NoNewline -ForegroundColor Green
        Write-Host "(built: $($winInfo.LastWriteTime))" -ForegroundColor DarkGray
    }
    if ($hasLinux) {
        Write-Host "  [L] Linux AppImage " -NoNewline -ForegroundColor Green
        Write-Host "($($linuxBuild.Name))" -ForegroundColor DarkGray
    }
    Write-Host ""

    # Check existing packages
    $existingPkgs = @()
    $patterns = @(
        @{ Pattern = "*$Version*setup*.exe"; Name = "Windows Installer" },
        @{ Pattern = "*$Version*portable*.zip"; Name = "Windows Portable ZIP" },
        @{ Pattern = "*$Version*linux*portable*"; Name = "Linux Portable" },
        @{ Pattern = "*$Version*.AppImage"; Name = "Linux AppImage" }
    )
    foreach ($p in $patterns) {
        $file = Get-ChildItem -Path $OrcaDir -Filter $p.Pattern -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($file) {
            $existingPkgs += "    [x] $($p.Name): $($file.Name)"
        }
    }
    if ($existingPkgs.Count -gt 0) {
        Write-Host "  Already packaged:" -ForegroundColor DarkCyan
        $existingPkgs | ForEach-Object { Write-Host $_ -ForegroundColor DarkGray }
        Write-Host ""
    }

    # Build the options list dynamically
    Write-Host "  What to create:" -ForegroundColor Cyan
    $optNum = 1
    $options = @{}

    if ($hasWin) {
        Write-Host "  [$optNum] Windows Installer (NSIS .exe)" -ForegroundColor White
        $options["$optNum"] = "win_installer"
        $optNum++

        Write-Host "  [$optNum] Windows Portable ZIP" -ForegroundColor White
        $options["$optNum"] = "win_portable"
        $optNum++

        Write-Host "  [$optNum] Windows BOTH (Installer + Portable)" -ForegroundColor White
        $options["$optNum"] = "win_both"
        $optNum++
    }
    if ($hasLinux) {
        Write-Host "  [$optNum] Linux Portable archive" -ForegroundColor White
        $options["$optNum"] = "linux_portable"
        $optNum++
    }
    if ($hasWin -and $hasLinux) {
        Write-Host "  [$optNum] ALL packages (Windows + Linux)" -ForegroundColor White
        $options["$optNum"] = "all"
        $optNum++
    }
    if ($hasWin) {
        Write-Host "  [R] Run OrcaSlicer" -ForegroundColor Cyan
    }
    Write-Host "  [0] Back to main menu" -ForegroundColor DarkGray
    Write-Host ""

    $pkgChoice = Read-Host "  Select"

    if ($pkgChoice -eq "R" -or $pkgChoice -eq "r") {
        if ($hasWin) {
            Write-Host "  Launching OrcaSlicer..." -ForegroundColor Cyan
            Start-Process $winBuild
        }
        return
    }
    if ($pkgChoice -eq "0" -or -not $options.ContainsKey($pkgChoice)) {
        return
    }

    $action = $options[$pkgChoice]

    switch ($action) {
        "win_installer"   { Build-Installer }
        "win_portable"    { Create-PortableZip }
        "win_both"        { Build-Installer; Create-PortableZip }
        "linux_portable"  { Create-LinuxPortableTar }
        "all" {
            Build-Installer
            Create-PortableZip
            Create-LinuxPortableTar
            Write-Host "`n  All packages created for v$Version!" -ForegroundColor Green
        }
    }
}

# ============================================================
# Release Functions
# ============================================================

function Push-ToGitHub {
    Write-Host "`n>>> Git Status..." -ForegroundColor Yellow
    Set-Location $OrcaDir
    & git status --short
    Write-Host ""

    $confirm = Read-Host "Push current branch to origin? (y/n)"
    if ($confirm -eq "y") {
        $branch = git branch --show-current
        & git push origin $branch
        Write-Host "`n  [OK] Pushed $branch to GitHub!" -ForegroundColor Green
    }
}

function Create-GitHubRelease {
    Write-Host "`n>>> Creating GitHub Release v$Version..." -ForegroundColor Yellow
    Set-Location $OrcaDir

    Write-Host "`n  Available release files:" -ForegroundColor Cyan
    $files = @()
    $patterns = @(
        @{ Pattern = "*$Version*portable*.zip"; Name = "Portable ZIP" },
        @{ Pattern = "*$Version*setup*.exe"; Name = "Installer EXE" },
        @{ Pattern = "*$Version*.AppImage"; Name = "Linux AppImage" }
    )

    foreach ($p in $patterns) {
        $file = Get-ChildItem -Path $OrcaDir -Filter $p.Pattern -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($file) {
            Write-Host "  [x] $($file.Name)" -ForegroundColor Green
            $files += $file.FullName
        } else {
            Write-Host "  [ ] $($p.Name) - NOT FOUND" -ForegroundColor Yellow
        }
    }

    if ($files.Count -eq 0) {
        Write-Host "`n  [ERROR] No release files found! Build first." -ForegroundColor Red
        return
    }

    Write-Host ""

    # Create tag if not exists
    $tagName = "v$Version"
    $tagExists = git tag -l $tagName
    if (-not $tagExists) {
        $confirm = Read-Host "Create and push tag $tagName? (y/n)"
        if ($confirm -eq "y") {
            & git tag -a $tagName -m "FilamentHub Edition $Version"
            & git push origin $tagName
            Write-Host "  Tag $tagName created and pushed" -ForegroundColor Green
        }
    } else {
        Write-Host "  Tag $tagName already exists" -ForegroundColor DarkGray
    }

    # Open GitHub release page
    Write-Host "`n  Opening GitHub release page..." -ForegroundColor Yellow
    Write-Host "`n  Upload these files:" -ForegroundColor Cyan
    foreach ($f in $files) {
        Write-Host "    $f" -ForegroundColor White
    }
    Start-Process "https://github.com/WeLizard/OrcaSlicer/releases/new?tag=$tagName"
    Write-Host "`n  [INFO] Complete the release in browser" -ForegroundColor Yellow
}

function Copy-ToServer {
    Write-Host "`n>>> Copying to Server (Samba)..." -ForegroundColor Yellow

    $serverPath = "\\192.168.0.33\FullDisk\home\lizard\FilamentHub\backend\distributions\orcaslicer"
    if (-not (Test-Path $serverPath)) {
        Write-Host "  [ERROR] Server path not accessible: $serverPath" -ForegroundColor Red
        Write-Host "  Make sure you're connected to the network" -ForegroundColor Yellow
        return
    }

    $patterns = @(
        "*$Version*portable*.zip",
        "*$Version*setup*.exe",
        "*$Version*.AppImage"
    )

    $copied = 0
    foreach ($pattern in $patterns) {
        $file = Get-ChildItem -Path $OrcaDir -Filter $pattern -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($file) {
            Write-Host "  Copying $($file.Name)..." -ForegroundColor Yellow
            Copy-Item $file.FullName $serverPath -Force
            Write-Host "    [OK] Copied" -ForegroundColor Green
            $copied++
        }
    }

    if ($copied -gt 0) {
        Write-Host "`n  [OK] $copied file(s) copied to server!" -ForegroundColor Green
    } else {
        Write-Host "`n  [WARNING] No files found for version $Version" -ForegroundColor Yellow
    }
}

# ============================================================
# Status & Settings
# ============================================================

function Check-BuildStatus {
    Write-Host "`n>>> Build Status for v$Version" -ForegroundColor Yellow
    Write-Host ""

    # Check slicer exe
    $slicerExe = Join-Path $BuildDir "OrcaSlicer/orca-slicer.exe"
    if (Test-Path $slicerExe) {
        $info = Get-Item $slicerExe
        Write-Host "  Slicer EXE:    " -NoNewline
        Write-Host "[OK]" -ForegroundColor Green -NoNewline
        Write-Host " Built: $($info.LastWriteTime)" -ForegroundColor DarkGray
    } else {
        Write-Host "  Slicer EXE:    " -NoNewline
        Write-Host "[NOT BUILT]" -ForegroundColor Red
    }

    # Check deps
    $depsPath = Join-Path $OrcaDir "deps/build/OrcaSlicer_dep"
    Write-Host "  Dependencies:  " -NoNewline
    if (Test-Path $depsPath) {
        Write-Host "[OK]" -ForegroundColor Green
    } else {
        Write-Host "[NOT BUILT]" -ForegroundColor Red
    }

    # Check version-specific files
    $checks = @(
        @{ Pattern = "*$Version*setup*.exe"; Name = "Installer" },
        @{ Pattern = "*$Version*portable*.zip"; Name = "Portable ZIP" },
        @{ Pattern = "*$Version*.AppImage"; Name = "Linux AppImage" }
    )

    foreach ($check in $checks) {
        $file = Get-ChildItem -Path $OrcaDir -Filter $check.Pattern -ErrorAction SilentlyContinue |
            Select-Object -First 1
        $label = "$($check.Name):".PadRight(15)
        Write-Host "  $label" -NoNewline
        if ($file) {
            Write-Host "[OK]" -ForegroundColor Green -NoNewline
            Write-Host " $($file.Name) ($([math]::Round($file.Length / 1MB, 1)) MB)" -ForegroundColor DarkGray
        } else {
            Write-Host "[NOT BUILT]" -ForegroundColor Yellow
        }
    }

    Write-Host ""

    # FilamentHub module status
    Write-Host "  FilamentHub Modules:" -ForegroundColor DarkCyan
    $moduleFiles = @(
        "src/slic3r/GUI/FilamentHubPanel.cpp",
        "src/slic3r/GUI/FilamentHubPanel.hpp",
        "src/slic3r/Utils/FilamentHubClient.cpp",
        "src/slic3r/Utils/FilamentHubClient.hpp"
    )
    foreach ($mf in $moduleFiles) {
        $fullPath = Join-Path $OrcaDir $mf
        $shortName = Split-Path $mf -Leaf
        Write-Host "    $shortName " -NoNewline
        if (Test-Path $fullPath) {
            $lines = (Get-Content $fullPath | Measure-Object -Line).Lines
            Write-Host "[OK] ${lines} lines" -ForegroundColor Green
        } else {
            Write-Host "[MISSING]" -ForegroundColor Red
        }
    }

    Write-Host ""

    # All release files
    Write-Host "  All release files:" -ForegroundColor DarkCyan
    $allFiles = Get-ChildItem -Path $OrcaDir -Filter "OrcaSlicer-FilamentHub-*" -ErrorAction SilentlyContinue
    if ($allFiles) {
        foreach ($f in $allFiles) {
            $size = [math]::Round($f.Length / 1MB, 1)
            Write-Host "    $($f.Name) ($size MB)" -ForegroundColor DarkGray
        }
    } else {
        Write-Host "    (none)" -ForegroundColor DarkGray
    }

    Write-Host ""

    # Git status
    Set-Location $OrcaDir
    Write-Host "  Git branch:    " -NoNewline
    $branch = git branch --show-current
    Write-Host "$branch" -ForegroundColor Cyan

    Write-Host "  Git status:    " -NoNewline
    $changes = git status --porcelain | Measure-Object -Line
    if ($changes.Lines -eq 0) {
        Write-Host "clean" -ForegroundColor Green
    } else {
        Write-Host "$($changes.Lines) uncommitted changes" -ForegroundColor Yellow
    }

    $upstream = Get-UpstreamInfo
    Write-Host "  Upstream main: " -NoNewline
    Write-Host "$($upstream.Main)" -ForegroundColor DarkGray
    if ($upstream.Release) {
        Write-Host "  Upstream rel:  " -NoNewline
        Write-Host "$($upstream.Release) ($($upstream.ReleaseBranch))" -ForegroundColor DarkCyan
    }

    Write-Host "  Upstream:      " -NoNewline
    $behind = git rev-list --count "HEAD..upstream/main" 2>$null
    if ($LASTEXITCODE -eq 0 -and $behind -gt 0) {
        Write-Host "$behind commits behind" -ForegroundColor Yellow
    } else {
        Write-Host "up to date (or no upstream)" -ForegroundColor Green
    }
}

function Change-Version {
    Write-Host "`n>>> Change Version" -ForegroundColor Yellow
    Write-Host ""
    $base = Get-BaseVersion
    Write-Host "  Base version (version.inc): $base" -ForegroundColor Cyan
    Write-Host "  Build version:              $Version" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Enter the base version. '+fh' will be appended automatically." -ForegroundColor DarkGray
    Write-Host "  Examples: 2.3.2-dev, 2.3.3, 2.4.0-rc1" -ForegroundColor DarkGray
    Write-Host ""

    $newVersion = Read-Host "  Enter new base version (or press Enter to cancel)"
    if ($newVersion -and $newVersion -ne "") {
        Set-BaseVersion $newVersion
        $script:Version = Get-FHVersion
        Write-Host "`n  [OK] version.inc -> $newVersion" -ForegroundColor Green
        Write-Host "  [OK] Build version -> $Version" -ForegroundColor Green
    } else {
        Write-Host "`n  [CANCELLED]" -ForegroundColor DarkGray
    }
}

function Sync-Upstream {
    Write-Host "`n>>> Sync from Upstream (SoftFever/OrcaSlicer)..." -ForegroundColor Yellow
    Set-Location $OrcaDir

    Write-Host "  Fetching upstream..." -ForegroundColor DarkGray
    & git fetch upstream main

    $behind = git rev-list --count "HEAD..upstream/main" 2>$null
    if ($LASTEXITCODE -ne 0 -or $behind -eq 0) {
        Write-Host "`n  [OK] Already up to date with upstream!" -ForegroundColor Green
        return
    }

    Write-Host "`n  You are $behind commits behind upstream/main" -ForegroundColor Yellow
    Write-Host ""

    Write-Host "  Recent upstream changes:" -ForegroundColor Cyan
    & git log "HEAD..upstream/main" --oneline -10
    Write-Host ""

    $confirm = Read-Host "Merge upstream/main into current branch? (y/n)"
    if ($confirm -eq "y") {
        & git merge upstream/main -m "Merge upstream main ($behind commits)"
        if ($LASTEXITCODE -eq 0) {
            Write-Host "`n  [OK] Merged successfully!" -ForegroundColor Green
            $push = Read-Host "Push to origin? (y/n)"
            if ($push -eq "y") {
                & git push origin HEAD
            }
        } else {
            Write-Host "`n  [CONFLICT] Merge conflicts detected!" -ForegroundColor Red
            Write-Host "  Resolve conflicts manually, then commit and push" -ForegroundColor Yellow
        }
    }
}

# ============================================================
# Menu
# ============================================================

function Show-Menu {
    Clear-Host
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host " OrcaSlicer FilamentHub Edition - Builder   " -ForegroundColor Cyan
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host ""
    $upstream = Get-UpstreamInfo
    Write-Host " FH Version:     " -NoNewline
    Write-Host "$Version" -ForegroundColor Yellow
    Write-Host " Upstream main:  " -NoNewline
    Write-Host "$($upstream.Main)" -ForegroundColor DarkGray
    if ($upstream.Release) {
        Write-Host " Upstream release:" -NoNewline
        Write-Host " $($upstream.Release)" -ForegroundColor DarkCyan -NoNewline
        Write-Host " ($($upstream.ReleaseBranch))" -ForegroundColor DarkGray
    }
    Write-Host " Dir: $OrcaDir" -ForegroundColor DarkGray
    Write-Host ""
    Write-Host " --- Quick Build (deps exist) ---" -ForegroundColor Green
    Write-Host " [1] Build Slicer (Release)" -ForegroundColor White
    Write-Host " [2] Build ALL Windows (Slicer + Installer + Portable)" -ForegroundColor White
    Write-Host " [3] Build Slicer (Debug)" -ForegroundColor White
    Write-Host " [4] Build Slicer (RelWithDebInfo)" -ForegroundColor White
    Write-Host " [5] Package existing build (Installer / Portable / both)" -ForegroundColor White
    Write-Host ""
    Write-Host " --- Full Build (from scratch) ---" -ForegroundColor Yellow
    Write-Host " [F] Full Build Windows (deps + slicer)" -ForegroundColor White
    Write-Host " [L] Full Build Linux (Docker + AppImage)" -ForegroundColor White
    Write-Host ""
    Write-Host " --- Dependencies & Packages ---" -ForegroundColor DarkCyan
    Write-Host " [D] Build Dependencies only" -ForegroundColor White
    Write-Host " [I] Build Installer (NSIS)" -ForegroundColor White
    Write-Host " [P] Create Portable ZIP" -ForegroundColor White
    Write-Host ""
    Write-Host " --- Release ---" -ForegroundColor Magenta
    Write-Host " [6] Push to GitHub" -ForegroundColor White
    Write-Host " [7] Create GitHub Release" -ForegroundColor White
    Write-Host " [8] Copy to Server (Samba)" -ForegroundColor White
    Write-Host ""
    Write-Host " --- Settings ---" -ForegroundColor DarkYellow
    Write-Host " [9] Check Build Status" -ForegroundColor White
    Write-Host " [V] Change Version" -ForegroundColor White
    Write-Host " [S] Sync from Upstream" -ForegroundColor White
    Write-Host " [C] Clean FilamentHub Cache" -ForegroundColor Red
    Write-Host " [X] Clean Build (delete build folder)" -ForegroundColor DarkRed
    Write-Host " [Z] NUCLEAR Clean (build + deps)" -ForegroundColor DarkRed
    Write-Host " [0] Exit" -ForegroundColor Red
    Write-Host ""
}

# ============================================================
# Main Loop
# ============================================================

Set-Location $OrcaDir

while ($true) {
    $Version = Get-FHVersion
    Show-Menu

    $choice = Read-Host "Select option"

    switch ($choice.ToUpper()) {
        # Quick Build
        "1" { Build-Slicer -BuildType "Release"; Read-Host "`nPress Enter..." }
        "2" { Build-AllWindows; Read-Host "`nPress Enter..." }
        "3" { Build-Debug; Read-Host "`nPress Enter..." }
        "4" { Build-RelWithDebInfo; Read-Host "`nPress Enter..." }
        "5" { Package-ExistingBuild; Read-Host "`nPress Enter..." }

        # Full Build
        "F" { Build-FullWindows; Read-Host "`nPress Enter..." }
        "L" { Build-FullLinux; Read-Host "`nPress Enter..." }

        # Dependencies & Packages
        "D" { Build-Deps; Read-Host "`nPress Enter..." }
        "I" { Build-Installer; Read-Host "`nPress Enter..." }
        "P" { Create-PortableZip; Read-Host "`nPress Enter..." }

        # Release
        "6" { Push-ToGitHub; Read-Host "`nPress Enter..." }
        "7" { Create-GitHubRelease; Read-Host "`nPress Enter..." }
        "8" { Copy-ToServer; Read-Host "`nPress Enter..." }

        # Settings
        "9" { Check-BuildStatus; Read-Host "`nPress Enter..." }
        "V" { Change-Version; Read-Host "`nPress Enter..." }
        "S" { Sync-Upstream; Read-Host "`nPress Enter..." }
        "C" { Clean-BuildCache; Read-Host "`nPress Enter..." }
        "X" { Clean-FullBuild; Read-Host "`nPress Enter..." }
        "Z" { Clean-Everything; Read-Host "`nPress Enter..." }
        "0" { Write-Host "`nBye!" -ForegroundColor Cyan; exit }
        default { Write-Host "`nInvalid option!" -ForegroundColor Red; Start-Sleep 1 }
    }
}
