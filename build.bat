@echo off
if exist "%ROOT%\bbxc.exe" del "%ROOT%\bbxc.exe"
if exist "%ROOT%\bbx.exe" del "%ROOT%\bbx.exe" 

SET ROOT=%CD%
SET SRC=%ROOT%\src

echo Building compiler...
cd "%SRC%"

cd "%SRC%\source"
cargo build
for %%f in ("%SRC%\source\target\debug\*.lib") do set RUSTLIB=%%~ff
cd "%SRC%"
cl compiler.c assembler/asm.c tools.c "%RUSTLIB%" bcrypt.lib ws2_32.lib userenv.lib iphlpapi.lib advapi32.lib ntdll.lib ole32.lib shell32.lib kernel32.lib /Fe:"%ROOT%\bbxc.exe"
if errorlevel 1 (
    echo Failed to build assembler
    exit /b 1
)

echo Building interpreter...
cd "%SRC%\interpreter"
cl *.c ../tools.c bcrypt.lib /Fe:"%ROOT%\bbx.exe"
if errorlevel 1 (
    echo Failed to build interpreter
    exit /b 1
)

echo Build complete!
cd "%ROOT%"