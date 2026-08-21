#Requires -Version 5.1
<#
.SYNOPSIS
    下载并解压 CEF Standard Binary 到 third_party/cef

.EXAMPLE
    .\scripts\setup-cef.ps1
    .\scripts\setup-cef.ps1 -Force
#>
[CmdletBinding()]
param(
    [string]$CefRoot = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path $PSScriptRoot -Parent
if (-not $CefRoot) {
    $CefRoot = Join-Path $ProjectRoot "third_party\cef"
}

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Ok([string]$Message) {
    Write-Host "    $Message" -ForegroundColor Green
}

if ((Test-Path (Join-Path $CefRoot "include\cef_app.h")) -and -not $Force) {
    Write-Ok "CEF already present: $CefRoot"
    exit 0
}

Write-Step "Query latest stable CEF windows64"
$idx = Invoke-RestMethod -Uri "https://cef-builds.spotifycdn.com/index.json"
$version = $idx.windows64.versions | Where-Object { $_.channel -eq "stable" } | Select-Object -First 1
$file = $version.files | Where-Object { $_.type -eq "standard" } | Select-Object -First 1
$url = "https://cef-builds.spotifycdn.com/$($file.name)"
Write-Ok "$($version.cef_version)"
Write-Ok $file.name

$downloadDir = Join-Path $ProjectRoot "third_party"
New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null
$archive = Join-Path $downloadDir $file.name

if (-not (Test-Path $archive)) {
    Write-Step "Download (~$([math]::Round($file.size / 1MB)) MB)"
    Write-Host "    $url"
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
    Write-Ok "Saved -> $archive"
} else {
    Write-Ok "Archive exists -> $archive"
}

Write-Step "Extract"
$extractParent = Join-Path $downloadDir "_cef_extract"
if (Test-Path $extractParent) {
    Remove-Item -Recurse -Force $extractParent
}
New-Item -ItemType Directory -Force -Path $extractParent | Out-Null
& tar -xjf $archive -C $extractParent
if ($LASTEXITCODE -ne 0) {
    throw "tar extract failed (exit $LASTEXITCODE)"
}

$inner = Get-ChildItem $extractParent -Directory | Select-Object -First 1
if (-not $inner) {
    throw "Extracted folder not found"
}

if (Test-Path $CefRoot) {
    Remove-Item -Recurse -Force $CefRoot
}
New-Item -ItemType Directory -Force -Path (Split-Path $CefRoot -Parent) | Out-Null
Move-Item -Path $inner.FullName -Destination $CefRoot
Remove-Item -Recurse -Force $extractParent

if (-not (Test-Path (Join-Path $CefRoot "include\cef_app.h"))) {
    throw "Invalid CEF layout under $CefRoot"
}

Write-Step "Done"
Write-Ok "CEF_ROOT -> $CefRoot"
Write-Host ""
Write-Host "Next:" -ForegroundColor Yellow
Write-Host "  .\build.ps1 -EnableCef -NoPackage -NoZip"
