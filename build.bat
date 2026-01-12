@echo off
if exist "%ROOT%\bbx-asm.exe" del "%ROOT%\bbx-asm.exe"
if exist "%ROOT%\blackbox.exe" del "%ROOT%\blackbox.exe" 

SET ROOT=%CD%
SET SRC=%ROOT%\src

echo Building assembler...
cd "%SRC%\assembler"
cl *.c /Fe:"%ROOT%\bbx-asm.exe"
if errorlevel 1 (
    echo Failed to build assembler
    exit /b 1
)

echo Building interpreter...
cd "%SRC%\interpreter"
cl *.c /Fe:"%ROOT%\blackbox.exe"
if errorlevel 1 (
    echo Failed to build interpreter
    exit /b 1
)

echo Build complete!
cd "%ROOT%"