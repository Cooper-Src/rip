@echo off
setlocal

cd /d "%~dp0"

echo ========================================
echo RIP - Building everything
echo ========================================
echo.

cmake -S .-B build

cmake --build build --config Release --target rip
autobuild-gui.bat
cmake --build build --config Release --target rip-token-codec-test
cmake --build build --config Release --target rip-token-huffman-test
cmake --build build --config Release --target rip-compression-test
cmake --build build --config Release --target rip-compression-bench
cmake --build build --config Release --target rip

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