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
    if exist "%SRC%\utils\*.obj" del /f /q "%SRC%\utils\*.obj"
    if exist "%ROOT%\*.obj" del /f /q "%ROOT%\*.obj"
    if exist "%ROOT%\target" rmdir /s /q "%ROOT%\target"
    echo Clean complete.
    exit /b 0
)

echo Building compiler...
clang-cl /std:c++latest /EHsc /D_CRT_NONSTDC_NO_WARNINGS /D_CRT_SECURE_NO_WARNINGS /I "%SRC%" /I "%SRC%\blackboxc" "%SRC%\blackboxc\compiler.cpp" "%SRC%\blackboxc\asm.cpp" "%SRC%\blackboxc\asm_util.cpp" "%SRC%\blackboxc\basic.cpp" "%SRC%\utils\data.cpp" "%SRC%\utils\string_utils.cpp" "%SRC%\utils\asm_parser.cpp" "%SRC%\utils\preprocessor.cpp" "%SRC%\utils\macro_expansion.cpp" "%SRC%\utils\symbol_table.cpp" "%SRC%\utils\random_utils.cpp" bcrypt.lib /Fe:"%ROOT%\bbxc.exe"
if errorlevel 1 (
    echo Failed to build compiler
    exit /b 1
)

echo Building interpreter...
clang-cl /std:c++latest /EHsc /D_CRT_NONSTDC_NO_WARNINGS /D_CRT_SECURE_NO_WARNINGS /I "%SRC%" /I "%SRC%\blackbox" /I "%SRC%\blackboxc" "%SRC%\blackbox\blackbox.cpp" "%SRC%\blackbox\debug.cpp" "%SRC%\utils\data.cpp" "%SRC%\utils\fmt.cpp" "%SRC%\utils\string_utils.cpp" "%SRC%\utils\asm_parser.cpp" "%SRC%\utils\preprocessor.cpp" "%SRC%\utils\macro_expansion.cpp" "%SRC%\utils\symbol_table.cpp" "%SRC%\utils\random_utils.cpp" bcrypt.lib /Fe:"%ROOT%\bbx.exe"
if errorlevel 1 (
    echo Failed to build interpreter
    exit /b 1
)

echo Building disassembler...
clang-cl /std:c++latest /EHsc /D_CRT_NONSTDC_NO_WARNINGS /D_CRT_SECURE_NO_WARNINGS /I "%SRC%" "%SRC%\blackboxd\blackboxd.cpp" "%SRC%\utils\data.cpp" /Fe:"%ROOT%\bbxd.exe"
if errorlevel 1 (
    echo Failed to build disassembler
    exit /b 1
)

echo Build complete!
exit /b 0