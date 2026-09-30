@echo off
setlocal EnableExtensions

set "RIP_ROOT=%~dp0.."
set "BUILD_DIR=%RIP_ROOT%\build"

cd /d "%RIP_ROOT%"

echo ========================================
echo RIP - Build All (ARM64)
echo ========================================
echo.

where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake was not found in PATH.
    echo.
    pause
    exit /b 1
)

if not exist "%RIP_ROOT%\CMakeLists.txt" (
    echo ERROR: CMakeLists.txt was not found.
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
        echo ========================================
        echo CONFIGURATION FAILED
        echo ========================================
        echo.
        pause
        exit /b 1
    )
)

echo Building all RIP targets...
echo.

cmake --build "%BUILD_DIR%" --config Release --parallel

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
echo Output:
echo   %BUILD_DIR%\Release\
echo.

if exist "%BUILD_DIR%\Release\rip.exe" (
    echo   [OK] rip.exe
) else (
    echo   [--] rip.exe
)

if exist "%BUILD_DIR%\Release\rip-gui.exe" (
    echo   [OK] rip-gui.exe
) else (
    echo   [--] rip-gui.exe
)

echo.
pause

endlocal
