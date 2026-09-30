@echo off
setlocal

cd /d "%~dp0"

echo ========================================
echo RIP - Building tests
echo ========================================
echo.

set FAILED=0
cmake -S .-B build
echo [1/5] Token codec test...
cmake --build build --config Release --target rip-token-codec-test
if errorlevel 1 set FAILED=1

echo.
echo [2/5] Token Huffman test...
cmake --build build --config Release --target rip-token-huffman-test
if errorlevel 1 set FAILED=1

echo.
echo [3/5] RIPC compression test...
cmake --build build --config Release --target rip-compression-test
if errorlevel 1 set FAILED=1

echo.
echo [4/5] RIPC compression benchmark...
cmake --build build --config Release --target rip-compression-bench
if errorlevel 1 set FAILED=1

echo.
echo [5/5] RIP executable...
cmake --build build --config Release --target rip
if errorlevel 1 set FAILED=1

echo.

if "%FAILED%"=="1" (
    echo ========================================
    echo One or more builds FAILED.
    echo ========================================
    exit /b 1
)

echo ========================================
echo All test targets built successfully.
echo ========================================

endlocal