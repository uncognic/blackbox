@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "SRC=%ROOT%\src"

if /I "%~1"=="clean" (
    echo Cleaning build artifacts...
    if exist "%ROOT%\bbxc.exe" del /f /q "%ROOT%\bbxc.exe"
    if exist "%ROOT%\bbx.exe" del /f /q "%ROOT%\bbx.exe"
    if exist "%ROOT%\bbxd.exe" del /f /q "%ROOT%\bbxd.exe"
    if exist "%SRC%\blackboxc\*.obj" del /f /q "%SRC%\blackboxc\*.obj"
    if exist "%SRC%\blackbox\*.obj" del /f /q "%SRC%\blackbox\*.obj"
    if exist "%ROOT%\*.obj" del /f /q "%ROOT%\*.obj"
    if exist "%ROOT%\target" rmdir /s /q "%ROOT%\target"
    echo Clean complete.
    exit /b 0
)

echo Building compiler...
clang-cl /D_CRT_NONSTDC_NO_WARNINGS /D_CRT_SECURE_NO_WARNINGS /I "%SRC%\blackboxc" "%SRC%\blackboxc\compiler.cpp" "%SRC%\blackboxc\asm.cpp" "%SRC%\blackboxc\asm_util.cpp" "%SRC%\blackboxc\basic.cpp" "%SRC%\blackboxc\tools.cpp" "%SRC%\data.cpp" bcrypt.lib /Fe:"%ROOT%\bbxc.exe"
if errorlevel 1 (
    echo Failed to build compiler
    exit /b 1
)

echo Building interpreter...
clang-cl /D_CRT_NONSTDC_NO_WARNINGS /D_CRT_SECURE_NO_WARNINGS /I "%SRC%\blackbox" /I "%SRC%\blackboxc" "%SRC%\blackbox\blackbox.c" "%SRC%\blackbox\debug.c" "%SRC%\blackboxc\tools.cpp" "%SRC%\data.cpp" bcrypt.lib /Fe:"%ROOT%\bbx.exe"
if errorlevel 1 (
    echo Failed to build interpreter
    exit /b 1
)

echo Building disassembler...
cargo build --release --manifest-path "%SRC%\bbx-disasm\Cargo.toml"
if errorlevel 1 (
    echo Failed to build disassembler
    exit /b 1
)

copy /Y "%ROOT%\target\release\bbxd.exe" "%ROOT%\bbxd.exe" >nul
if errorlevel 1 (
    echo Failed to copy disassembler
    exit /b 1
)

echo Build complete!
exit /b 0