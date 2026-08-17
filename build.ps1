#Requires -Version 5.1
<#
.SYNOPSIS
    HgyPlayer one-click build and package (Windows / Qt)

.DESCRIPTION
    Default toolchain: MSVC2022 + Qt msvc2022_64 (matches typical Desktop Qt MSVC kit).
    Also supports MinGW via -Toolchain mingw.
    Builds, runs windeployqt, copies FFmpeg DLLs, outputs dist\HgyPlayer\ and zip.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Clean
    .\build.ps1 -Toolchain mingw
    .\build.ps1 -QtVersion 6.10.2 -NoZip
#>
[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",

    # msvc (default) | mingw
    [ValidateSet("msvc", "mingw")]
    [string]$Toolchain = "msvc",

    # e.g. 6.10.2; empty = newest matching Qt6 kit
    [string]$QtVersion = "",

    [string]$QtInstallRoot = "C:\Qt",

    [string]$BuildDirName = "build-package",

    [switch]$Clean,
    [switch]$NoPackage,
    [switch]$NoZip,

    [int]$Jobs = 0
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Ok([string]$Message) {
    Write-Host "    $Message" -ForegroundColor Green
}

function Write-Fail([string]$Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

function Resolve-NewestQtKit {
    param(
        [string]$Root,
        [string]$PreferVersion,
        [string]$KitName   # mingw_64 | msvc2022_64
    )

    if (-not (Test-Path $Root)) {
        Write-Fail "Qt install root not found: $Root (use -QtInstallRoot)"
    }

    $candidates = @()
    Get-ChildItem $Root -Directory -ErrorAction SilentlyContinue | ForEach-Object {
        if ($_.Name -match '^(\d+)\.(\d+)\.(\d+)$') {
            $kit = Join-Path $_.FullName $KitName
            $cmakeConfig = Join-Path $kit "lib\cmake\Qt6\Qt6Config.cmake"
            $windeploy = Join-Path $kit "bin\windeployqt.exe"
            if ((Test-Path $cmakeConfig) -and (Test-Path $windeploy)) {
                $candidates += [pscustomobject]@{
                    Version = $_.Name
                    Major   = [int]$Matches[1]
                    Minor   = [int]$Matches[2]
                    Patch   = [int]$Matches[3]
                    KitPath = $kit
                }
            }
        }
    }

    if (-not $candidates) {
        Write-Fail "No Qt6 $KitName kit found under $Root"
    }

    if ($PreferVersion) {
        $hit = $candidates | Where-Object { $_.Version -eq $PreferVersion } | Select-Object -First 1
        if (-not $hit) {
            $available = ($candidates | ForEach-Object { $_.Version }) -join ", "
            Write-Fail "Qt $PreferVersion $KitName not found. Available: $available"
        }
        return $hit
    }

    return $candidates |
        Sort-Object Major, Minor, Patch -Descending |
        Select-Object -First 1
}

function Resolve-Tool([string[]]$Candidates, [string]$Name) {
    foreach ($c in $Candidates) {
        if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path }
    }
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Import-VsDevEnvironment {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $vsDevCmd = $null

    if (Test-Path $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($installPath) {
            $candidate = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
            if (Test-Path $candidate) { $vsDevCmd = $candidate }
        }
    }

    if (-not $vsDevCmd) {
        $fallbacks = @(
            "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
            "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat",
            "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
            "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
        )
        foreach ($f in $fallbacks) {
            if (Test-Path $f) { $vsDevCmd = $f; break }
        }
    }

    if (-not $vsDevCmd) {
        Write-Fail "VsDevCmd.bat not found. Install VS 2022/2025 with C++ desktop workload."
    }

    Write-Ok "VsDevCmd -> $vsDevCmd"

    $tempEnv = [System.IO.Path]::GetTempFileName()
    try {
        $cmdLine = "`"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && set > `"$tempEnv`""
        cmd.exe /c $cmdLine | Out-Null
        if (-not (Test-Path $tempEnv)) {
            Write-Fail "Failed to capture VS environment"
        }

        Get-Content $tempEnv | ForEach-Object {
            if ($_ -match '^(.*?)=(.*)$') {
                $name = $Matches[1]
                $value = $Matches[2]
                [System.Environment]::SetEnvironmentVariable($name, $value, "Process")
            }
        }
    }
    finally {
        Remove-Item -Force $tempEnv -ErrorAction SilentlyContinue
    }

    $cl = Get-Command cl.exe -ErrorAction SilentlyContinue
    if (-not $cl) {
        Write-Fail "cl.exe not on PATH after VsDevCmd (MSVC C++ tools missing?)"
    }
    Write-Ok "cl -> $($cl.Source)"
}

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

$FfmpegRoot = Join-Path $ProjectRoot "ffmpeg-master-latest-win64-lgpl-shared"
$BuildDir = Join-Path $ProjectRoot $BuildDirName
$DistDir = Join-Path $ProjectRoot "dist\HgyPlayer"
$ZipPath = Join-Path $ProjectRoot "dist\HgyPlayer-$Config-$Toolchain-win64.zip"

Write-Host "HgyPlayer one-click build" -ForegroundColor White
Write-Host "  Project   : $ProjectRoot"
Write-Host "  Config    : $Config"
Write-Host "  Toolchain : $Toolchain"

if (-not (Test-Path (Join-Path $FfmpegRoot "include\libavformat\avformat.h"))) {
    Write-Fail "FFmpeg headers missing: $FfmpegRoot"
}

# ---------------------------------------------------------------------------
# Detect toolchain
# ---------------------------------------------------------------------------
Write-Step "Detect Qt / toolchain"

$kitName = if ($Toolchain -eq "msvc") { "msvc2022_64" } else { "mingw_64" }
$qt = Resolve-NewestQtKit -Root $QtInstallRoot -PreferVersion $QtVersion -KitName $kitName
$QtKit = $qt.KitPath
$QtBin = Join-Path $QtKit "bin"
$WinDeployQt = Join-Path $QtBin "windeployqt.exe"

Write-Ok "Qt $($qt.Version) $kitName -> $QtKit"

$Cmake = Resolve-Tool @(
    (Join-Path $QtInstallRoot "Tools\CMake_64\bin\cmake.exe"),
    "C:\Program Files\CMake\bin\cmake.exe"
) "cmake.exe"
if (-not $Cmake) { Write-Fail "cmake.exe not found" }
Write-Ok "cmake -> $Cmake"

$Ninja = Resolve-Tool @(
    (Join-Path $QtInstallRoot "Tools\Ninja\ninja.exe")
) "ninja.exe"

$toolPaths = @($QtBin, (Split-Path $Cmake -Parent))
if ($Ninja) { $toolPaths += (Split-Path $Ninja -Parent) }

if ($Toolchain -eq "msvc") {
    Import-VsDevEnvironment
    if ($Ninja) {
        $Generator = "Ninja"
    } else {
        $Generator = "NMake Makefiles"
    }
} else {
    $MingwCandidates = @(
        (Join-Path $QtInstallRoot "Tools\mingw1310_64\bin"),
        (Join-Path $QtInstallRoot "Tools\mingw1120_64\bin"),
        (Join-Path $QtInstallRoot "Tools\mingw810_64\bin")
    ) | Where-Object { Test-Path (Join-Path $_ "g++.exe") }

    if (-not $MingwCandidates) {
        Write-Fail "MinGW g++ not found (expected C:\Qt\Tools\mingw1310_64 etc.)"
    }
    $MingwBin = $MingwCandidates[0]
    Write-Ok "MinGW -> $MingwBin"
    $toolPaths = @($MingwBin) + $toolPaths

    if ($Ninja) {
        $Generator = "Ninja"
    } else {
        $Generator = "MinGW Makefiles"
    }
}

$env:PATH = ($toolPaths + $env:PATH) -join ";"
Write-Ok "Generator -> $Generator"

if ($Jobs -le 0) {
    $Jobs = [Math]::Max(1, [int]$env:NUMBER_OF_PROCESSORS)
}

# ---------------------------------------------------------------------------
# Configure / Build
# ---------------------------------------------------------------------------
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "Clean build directory"
    Remove-Item -Recurse -Force $BuildDir
    Write-Ok "Removed $BuildDir"
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Write-Step "CMake Configure ($Config / $Toolchain)"
$cmakeConfigureArgs = @(
    "-S", $ProjectRoot,
    "-B", $BuildDir,
    "-G", $Generator,
    "-DCMAKE_BUILD_TYPE=$($Config)",
    "-DCMAKE_PREFIX_PATH=$($QtKit)"
)

if ($Toolchain -eq "mingw") {
    $cmakeConfigureArgs += "-DCMAKE_CXX_COMPILER=$($MingwBin)\g++.exe"
} else {
    # Prefer explicit cl after VsDevCmd so Ninja does not pick up gcc from PATH
    $cl = (Get-Command cl.exe).Source
    $cmakeConfigureArgs += "-DCMAKE_CXX_COMPILER=$cl"
    $cmakeConfigureArgs += "-DCMAKE_C_COMPILER=$cl"
}

Write-Host ("    " + ($cmakeConfigureArgs -join " "))
& $Cmake @cmakeConfigureArgs
if ($LASTEXITCODE -ne 0) { Write-Fail "CMake configure failed (exit $LASTEXITCODE)" }

Write-Step "CMake Build ($Config, jobs=$Jobs)"
$cmakeBuildArgs = @(
    "--build", $BuildDir,
    "--parallel", "$Jobs"
)
if ($Generator -match "Visual Studio|Xcode") {
    $cmakeBuildArgs += @("--config", $Config)
}
& $Cmake @cmakeBuildArgs
if ($LASTEXITCODE -ne 0) { Write-Fail "Build failed (exit $LASTEXITCODE)" }

$Exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "HgyPlayer.exe" |
    Where-Object { $_.FullName -notmatch '\\CMakeFiles\\' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $Exe) { Write-Fail "HgyPlayer.exe not found under $BuildDir" }
Write-Ok "Binary -> $($Exe.FullName)"

if ($NoPackage) {
    Write-Step "Done (package skipped)"
    exit 0
}

# ---------------------------------------------------------------------------
# Package
# ---------------------------------------------------------------------------
Write-Step "Prepare dist folder"
if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Copy-Item -Force $Exe.FullName (Join-Path $DistDir "HgyPlayer.exe")

$buildExeDir = $Exe.DirectoryName
Get-ChildItem $buildExeDir -Filter "*.dll" -ErrorAction SilentlyContinue |
    ForEach-Object { Copy-Item -Force $_.FullName $DistDir }

Write-Step "windeployqt (Widgets + Multimedia)"
$deployMode = "--" + $Config.ToLowerInvariant()
& $WinDeployQt `
    $deployMode `
    --compiler-runtime `
    --no-translations `
    --multimedia `
    (Join-Path $DistDir "HgyPlayer.exe")
if ($LASTEXITCODE -ne 0) { Write-Fail "windeployqt failed (exit $LASTEXITCODE)" }

Write-Step "Copy FFmpeg runtime DLLs"
$ffmpegPatterns = @(
    "avutil-*.dll",
    "avcodec-*.dll",
    "avformat-*.dll",
    "swscale-*.dll",
    "swresample-*.dll"
)
$copied = 0
foreach ($pat in $ffmpegPatterns) {
    $matched = Get-ChildItem (Join-Path $FfmpegRoot "bin") -Filter $pat -ErrorAction SilentlyContinue
    foreach ($dll in $matched) {
        Copy-Item -Force $dll.FullName $DistDir
        $copied++
        Write-Ok $dll.Name
    }
}
if ($copied -eq 0) {
    Write-Fail "No FFmpeg DLLs found in $($FfmpegRoot)\bin"
}

if (-not $NoZip) {
    Write-Step "Create zip"
    New-Item -ItemType Directory -Force -Path (Split-Path $ZipPath -Parent) | Out-Null
    if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
    Compress-Archive -Path $DistDir -DestinationPath $ZipPath -CompressionLevel Optimal
    Write-Ok $ZipPath
}

Write-Step "Package complete"
Write-Host "  Dist : $DistDir"
if (-not $NoZip) {
    Write-Host "  Zip  : $ZipPath"
}
Write-Host "  Exe  : $(Join-Path $DistDir 'HgyPlayer.exe')"
Write-Host ""
Write-Host "Run: dist\HgyPlayer\HgyPlayer.exe" -ForegroundColor Green