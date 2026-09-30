@echo off
setlocal

echo ========================================
echo RIP CLI Auto Build
echo ========================================
echo.

set "RIP_ROOT=%~dp0"
set "BUILD_DIR=%RIP_ROOT%build"

echo Project:
echo %RIP_ROOT%
echo.

if not exist "%RIP_ROOT%CMakeLists.txt" (
    echo ERROR: CMakeLists.txt not found:
    echo %RIP_ROOT%CMakeLists.txt
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

echo Building RIP CLI...
echo.

cd /d "%RIP_ROOT%"
if errorlevel 1 (
    echo ERROR: Could not enter RIP directory.
    echo.
    pause
    exit /b 1
)
cmake -S .-B build
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
echo %BUILD_DIR%\Release\rip.exe
echo.

pause
exit /b 0