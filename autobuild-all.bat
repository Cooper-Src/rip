@echo off
setlocal EnableExtensions

cd /d "%~dp0"

echo ========================================
echo RIP - Build All
echo ========================================
echo.

where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake was not found in PATH.
    echo.
    pause
    exit /b 1
)

if not exist "CMakeLists.txt" (
    echo ERROR: CMakeLists.txt was not found.
    echo Make sure this script is inside the RIP project folder.
    echo.
    pause
    exit /b 1
)

if not exist "build\CMakeCache.txt" (
    echo Build directory is not configured.
    echo Configuring RIP for Visual Studio 2026 ARM64...
    echo.

    cmake -S . -B build -G "Visual Studio 18 2026" -A ARM64

    if errorlevel 1 (
        echo.
        echo ========================================
        echo CONFIGURATION FAILED
        echo ========================================
        echo.
        pause
        exit /b 1
    )

    echo.
)

echo Building all RIP targets...
echo.

cmake --build build --config Release --parallel

if errorlevel 1 (
    echo.
    echo ========================================
    echo BUILD FAILED
    echo ========================================
    echo.
    pause
    exit /b 1
)

echo.
echo ========================================
echo BUILD SUCCESSFUL
echo ========================================
echo.

echo Built output:
echo   build\Release\
echo.

if exist "build\Release\rip.exe" (
    echo   [OK] rip.exe
) else (
    echo   [--] rip.exe
)

if exist "build\Release\rip-gui.exe" (
    echo   [OK] rip-gui.exe
) else (
    echo   [--] rip-gui.exe
)

echo.
pause

endlocal