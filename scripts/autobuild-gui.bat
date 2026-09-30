@echo off
setlocal EnableExtensions

set "RIP_ROOT=%~dp0.."
set "GUI_DIR=%RIP_ROOT%\gui"
set "BUILD_DIR=%RIP_ROOT%\build"

cd /d "%RIP_ROOT%"

echo ========================================
echo RIP GUI Build (ARM64)
echo ========================================
echo.

if not exist "%RIP_ROOT%\CMakeLists.txt" (
    echo ERROR: CMakeLists.txt not found.
    echo.
    pause
    exit /b 1
)

if not exist "%GUI_DIR%\package.json" (
    echo ERROR: GUI package.json not found.
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

where npm >nul 2>&1
if errorlevel 1 (
    echo ERROR: npm was not found in PATH.
    echo.
    pause
    exit /b 1
)

echo [1/2] Building web GUI...
echo.

cd /d "%GUI_DIR%"
call npm run build

if errorlevel 1 (
    echo.
    echo ERROR: Web GUI build failed.
    echo.
    pause
    exit /b 1
)

echo.
echo [2/2] Building native RIP GUI...
echo.

cmake --build "%BUILD_DIR%" --config Release --target rip-gui

if errorlevel 1 (
    echo.
    echo ERROR: Native RIP GUI build failed.
    echo.
    pause
    exit /b 1
)

echo.
echo ========================================
echo RIP GUI BUILD SUCCESSFUL
echo ========================================
echo.
echo Executable:
echo   %BUILD_DIR%\Release\rip-gui.exe
echo.

pause
endlocal
