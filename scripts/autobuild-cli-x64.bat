@echo off
setlocal EnableExtensions

set "RIP_ROOT=%~dp0.."
set "BUILD_DIR=%RIP_ROOT%\build-x64"

cd /d "%RIP_ROOT%"

echo ========================================
echo RIP CLI Build (x64)
echo ========================================
echo.

if not exist "%RIP_ROOT%\CMakeLists.txt" (
    echo ERROR: CMakeLists.txt not found.
    echo.
    pause
    exit /b 1
)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo Configuring Visual Studio 2026 x64...
    echo.
    cmake -S "%RIP_ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 18 2026" -A x64

    if errorlevel 1 (
        echo.
        echo ERROR: CMake configuration failed.
        echo.
        pause
        exit /b 1
    )
)

echo Building RIP CLI...
echo.

cmake --build "%BUILD_DIR%" --config Release --target rip

if errorlevel 1 (
    echo.
    echo ERROR: RIP CLI build failed.
    echo.
    pause
    exit /b 1
)

echo.
echo ========================================
echo RIP CLI BUILD SUCCESSFUL
echo ========================================
echo.
echo Executable:
echo   %BUILD_DIR%\Release\rip.exe
echo.

pause
endlocal
