# ============================================================
#  servoV6 - Build & Launch GUI Application
#  Based on .vscode/settings.json configuration.
#  Run: .\build_and_run.ps1
# ============================================================

$ErrorActionPreference = "Stop"

# ---- Environment paths (sync with .vscode/settings.json) ----
$MingwBin     = "D:/develop/qt/qt6/Tools/mingw1310_64/bin"
$CmakeExe     = "D:/develop/qt/qt6/Tools/CMake_64/bin/cmake.exe"
$QtPrefix     = "D:/develop/qt/qt6/6.9.1/mingw_64"
$QtBin        = "D:/develop/qt/qt6/6.9.1/mingw_64/bin"
$BuildParallel = 32

# ---- Project paths ----
$ProjectRoot  = $PSScriptRoot
$BuildDir     = Join-Path $ProjectRoot "build"
$AppExe       = Join-Path $BuildDir "appservoV6.exe"

# ---- Setup PATH ----
$env:PATH = "$MingwBin;$QtBin;$env:PATH"

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "[1/3] Configuring CMake (MinGW Makefiles, Debug)..." -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

$cmakeArgs = @(
    "-S", $ProjectRoot,
    "-B", $BuildDir,
    "-G", "MinGW Makefiles",
    "-DCMAKE_BUILD_TYPE=Debug",
    "-DCMAKE_CXX_STANDARD=20",
    "-DCMAKE_AUTOMOC=OFF",
    "-DCMAKE_AUTORCC=OFF",
    "-DCMAKE_AUTOUIC=OFF",
    "-DCMAKE_MAKE_PROGRAM=$MingwBin/mingw32-make.exe",
    "-DCMAKE_C_COMPILER=$MingwBin/gcc.exe",
    "-DCMAKE_CXX_COMPILER=$MingwBin/g++.exe",
    "-DCMAKE_PREFIX_PATH=$QtPrefix"
)

& $CmakeExe @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] CMake configuration failed!" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "[2/3] Building appservoV6 (parallel: $BuildParallel)..." -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

$sw = [System.Diagnostics.Stopwatch]::StartNew()
& $CmakeExe --build $BuildDir --target appservoV6 --parallel $BuildParallel
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Build failed!" -ForegroundColor Red
    exit $LASTEXITCODE
}
$sw.Stop()
$elapsed = "{0:D2}:{1:D2}.{2:D3}" -f $sw.Elapsed.Minutes, $sw.Elapsed.Seconds, $sw.Elapsed.Milliseconds

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "[3/3] Launching appservoV6..." -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "Build time: $elapsed" -ForegroundColor Green
Write-Host ""

Start-Process -FilePath $AppExe
Write-Host "Application launched in a new window." -ForegroundColor Green
