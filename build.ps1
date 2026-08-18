# MS Windows PowerShell Build Script for Pi Pico / Pico W
# rev 2 - August 2026

param(
    [ValidateSet("basic", "wlan")]
    [string]$Target = "basic",

    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

# Use paths matching blink project
$env:CC  = "C:/DEV/vhd_mounts/msys2/msys64/mingw64/bin/gcc.exe"
$env:CXX = "C:/DEV/vhd_mounts/msys2/msys64/mingw64/bin/g++.exe"

$env:Path =
    "C:\DEV\vhd_mounts\msys2\msys64\mingw64\bin;" +
    "C:\DEV\arm_gnu_toolchains\15.2.rel1\bin;" +
    "C:\DEV\tools\bin;" +
    "C:\DEV\tools\cmake\bin;" +
    $env:Path

switch ($Target) {
    "wlan" {
        $picoBoard = "pico_w"
    }

    "basic" {
        $picoBoard = "pico"
    }
}

# Use a separate directory for each board and configuration.
# This avoids reusing a CMake cache configured for another board.
$buildDir = "build-$picoBoard-$Config"

Write-Host ""
Write-Host "========================================"
Write-Host " Building $Config configuration"
Write-Host " Target board: $picoBoard"
Write-Host "========================================"
Write-Host ""
Write-Host "Build directory: $buildDir"
Write-Host ""

Write-Host "ARM Compiler:"
arm-none-eabi-gcc --version | Select-Object -First 1

Write-Host "WIN Compiler for picotool:"
& $env:CC --version | Select-Object -First 1

Write-Host ""

cmake `
    -S . `
    -B $buildDir `
    -G Ninja `
    "-DCMAKE_BUILD_TYPE:STRING=$Config" `
    "-DPICO_BOARD:STRING=$picoBoard"

cmake --build $buildDir

