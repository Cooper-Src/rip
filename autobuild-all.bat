@echo off
setlocal

cd /d "%~dp0"

echo ========================================
echo RIP - Building everything
echo ========================================
echo.

cmake --build build --config Release --target ALL_BUILD

if errorlevel 1 (
    echo.
    echo BUILD FAILED.
    exit /b 1
)

echo.
echo ========================================
echo Build complete.
echo ========================================

endlocal