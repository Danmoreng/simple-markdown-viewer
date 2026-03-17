[CmdletBinding()]
param (
    [switch]$Clean,
    [switch]$UseNinja,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$BuildDir = "build",
    [string]$Target = "mdviewer",
    [switch]$RunSmokeTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Import-VSEnv {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Visual Studio might not be installed."
    }

    $installationPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installationPath) {
        throw "Visual Studio with C++ tools not found."
    }

    $vcvars = Join-Path $installationPath "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars)) {
        throw "vcvars64.bat not found at $vcvars"
    }

    Write-Host "Importing MSVC environment..." -ForegroundColor Cyan
    $tempFile = [IO.Path]::GetTempFileName()
    cmd /c "`"$vcvars`" && set > `"$tempFile`""
    Get-Content $tempFile | Foreach-Object {
        if ($_ -match "^(.*?)=(.*)$") {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }
    Remove-Item $tempFile
}

function Get-ConfiguredGenerator {
    param($Dir)
    $cacheFile = Join-Path $Dir "CMakeCache.txt"
    if (Test-Path $cacheFile) {
        $line = Get-Content $cacheFile | Select-String "CMAKE_GENERATOR:INTERNAL="
        if ($line -match "=(.*)$") {
            return $matches[1]
        }
    }
    return $null
}

# Ensure build directory exists
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory $BuildDir | Out-Null
}

# Import MSVC environment if not already present
if (-not $env:VCINSTALLDIR) {
    Import-VSEnv
}

if ($UseNinja -and -not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw "Ninja was requested with -UseNinja but was not found on PATH."
}

$generator = if ($UseNinja) { "Ninja" } else { "Visual Studio 17 2022" }
$currentGenerator = Get-ConfiguredGenerator $BuildDir

if ($currentGenerator -and $currentGenerator -ne $generator) {
    Write-Host "Generator mismatch (found $currentGenerator, requested $generator). Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
    New-Item -ItemType Directory $BuildDir | Out-Null
}

Write-Host "Configuring CMake with $generator..." -ForegroundColor Cyan
$configureArgs = @("-S", ".", "-B", $BuildDir, "-G", $generator)
if ($generator -eq "Ninja") {
    $configureArgs += "-DCMAKE_BUILD_TYPE=$Configuration"
}
cmake @configureArgs

Write-Host "Building target $Target ($Configuration)..." -ForegroundColor Cyan
cmake --build $BuildDir --config $Configuration --target $Target

if ($RunSmokeTest) {
    Write-Host "Running smoke test..." -ForegroundColor Cyan
    $exePath = if ($generator -match "Visual Studio") {
        Join-Path $BuildDir $Configuration "$Target.exe"
    } else {
        Join-Path $BuildDir "$Target.exe"
    }

    if (Test-Path $exePath) {
        Write-Host "Launching $exePath..." -ForegroundColor Green
        # GUI app, just check if it starts
        Start-Process $exePath
        Start-Sleep -Seconds 2
        Get-Process $Target -ErrorAction SilentlyContinue | Stop-Process
        Write-Host "Smoke test passed (app launched and closed)." -ForegroundColor Green
    } else {
        throw "Executable not found at $exePath"
    }
}
