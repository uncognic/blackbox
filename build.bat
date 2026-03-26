@echo off
SET ROOT=%CD%
SET SRC=%ROOT%\src

if /I "%~1"=="clean" (
    echo Cleaning build artifacts...
    if exist "%ROOT%\bbxc.exe" del /f /q "%ROOT%\bbxc.exe"
    if exist "%ROOT%\bbx.exe" del /f /q "%ROOT%\bbx.exe"
    if exist "%ROOT%\bbxd.exe" del /f /q "%ROOT%\bbxd.exe"
    if exist "%SRC%\assembler\*.obj" del /f /q "%SRC%\assembler\*.obj"
    if exist "%SRC%\interpreter\*.obj" del /f /q "%SRC%\interpreter\*.obj"
    if exist "%SRC%\*.obj" del /f /q "%SRC%\*.obj"
    rd /s /q "target" 2>nul
    echo Clean complete.
    exit /b 0
)

if /I "%~1"=="cargo" (
    shift
    echo Running %*
    cd "%SRC%\bbxc-bblang"
    %*
    exit /b %errorlevel%
)

if /I "%~1"=="rust" (
    echo Building Rust project...
    cd "%SRC%\bbxc-bblang"
    cargo build --release
    exit /b %errorlevel%
)

echo Building compiler...
cd "%SRC%"

cd "%SRC%\bbxc-bblang"
cargo build --release
for %%f in ("%ROOT%\target\release\*.lib") do set RUSTLIB=%%~ff
cd "%SRC%"
cl bbxc-frontend/compiler.c assembler/asm.c bbxc-frontend/tools.c "%RUSTLIB%" bcrypt.lib ws2_32.lib userenv.lib iphlpapi.lib advapi32.lib ntdll.lib ole32.lib shell32.lib kernel32.lib /Fe:"%ROOT%\bbxc.exe"
if errorlevel 1 (
    echo Failed to build assembler
    exit /b 1
)
echo Building disassembler...
cargo build --release --manifest-path=%SRC%\bbx-disasm\Cargo.toml
if errorlevel 1 (
    echo Failed to build disassembler
    exit /b 1
)
copy %ROOT%\target\release\bbxd.exe "%ROOT%\bbxd.exe" >nul
if errorlevel 1 (
    echo Failed to copy disassembler
    exit /b 1
)
echo Building interpreter...
cd "%SRC%\interpreter"
cl *.c ../bbxc-frontend/tools.c bcrypt.lib /Fe:"%ROOT%\bbx.exe"
if errorlevel 1 (
    echo Failed to build interpreter
    exit /b 1
)

echo Build complete!
cd "%ROOT%"