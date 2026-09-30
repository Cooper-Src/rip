@echo off
setlocal EnableExtensions

set "RIP_ROOT=%~dp0.."
set "BUILD_DIR=%RIP_ROOT%\build"

cd /d "%RIP_ROOT%"

echo ========================================
echo RIP - Build Tests (ARM64)
echo ========================================
echo.

if not exist "%RIP_ROOT%\CMakeLists.txt" (
    echo ERROR: CMakeLists.txt not found.
    echo.
    pause
    exit /b 1
)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo Configuring Visual Studio 2026 ARM64...
    echo.
    cmake -S "%RIP_ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 18 2026" -A ARM64

    if errorlevel 1 (
        echo.
        echo ERROR: CMake configuration failed.
        echo.
        pause
        exit /b 1
    )
)

set "FAILED=0"

echo [1/5] Token codec test...
cmake --build "%BUILD_DIR%" --config Release --target rip-token-codec-test
if errorlevel 1 set "FAILED=1"

echo.
echo [2/5] Token Huffman test...
cmake --build "%BUILD_DIR%" --config Release --target rip-token-huffman-test
if errorlevel 1 set "FAILED=1"

echo.
echo [3/5] RIPC compression test...
cmake --build "%BUILD_DIR%" --config Release --target rip-compression-test
if errorlevel 1 set "FAILED=1"

echo.
echo [4/5] RIPC compression benchmark...
cmake --build "%BUILD_DIR%" --config Release --target rip-compression-bench
if errorlevel 1 set "FAILED=1"

echo.
echo [5/5] RIP executable...
cmake --build "%BUILD_DIR%" --config Release --target rip
if errorlevel 1 set "FAILED=1"

echo.

if "%FAILED%"=="1" (
    echo ========================================
    echo One or more test targets FAILED.
    echo ========================================
    echo.
    pause
    exit /b 1
)

echo ========================================
echo All test targets built successfully.
echo ========================================
echo.

pause
endlocal
