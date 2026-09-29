@echo off
setlocal

echo ========================================
echo RIP GUI Auto Build
echo ========================================
echo.

set "RIP_ROOT=%~dp0"
set "GUI_DIR=%RIP_ROOT%gui"
set "BUILD_DIR=%RIP_ROOT%build"

echo Project:
echo %RIP_ROOT%
echo.

if not exist "%GUI_DIR%\package.json" (
    echo ERROR: GUI package.json not found:
    echo %GUI_DIR%\package.json
    echo.
    pause
    exit /b 1
)

if not exist "%BUILD_DIR%" (
    echo ERROR: CMake build directory not found:
    echo %BUILD_DIR%
    echo.
    echo Run the CMake configure command first.
    echo.
    pause
    exit /b 1
)

echo [1/2] Building web GUI...
echo.

cd /d "%GUI_DIR%"
if errorlevel 1 (
    echo ERROR: Could not enter GUI directory.
    echo.
    pause
    exit /b 1
)

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

cd /d "%RIP_ROOT%"
if errorlevel 1 (
    echo ERROR: Could not return to RIP directory.
    echo.
    pause
    exit /b 1
)

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
echo %BUILD_DIR%\Release\rip-gui.exe
echo.

pause
exit /b 0