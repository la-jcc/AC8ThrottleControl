@echo off
setlocal
set "AC8_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "AC8_VS="
if exist "%AC8_VSWHERE%" for /f "usebackq delims=" %%I in (`"%AC8_VSWHERE%" -version [17.0^,18.0^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "AC8_VS=%%I"
if not defined AC8_VS set "AC8_VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%AC8_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if %errorlevel% neq 0 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\lua_loader_source_tests.ps1
if %errorlevel% neq 0 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\bridge_source_tests.ps1
if %errorlevel% neq 0 exit /b 1
cl /nologo /O2 /MT /W4 /std:c++20 /EHsc /Isrc tests\control_state_test.cpp /Fo:build\control_state_test.obj /Fe:build\control_state_test.exe
if %errorlevel% neq 0 exit /b 1
build\control_state_test.exe
if %errorlevel% neq 0 exit /b 1
cl /nologo /O2 /MT /W4 /std:c++20 /EHsc /Isrc tests\raw_axis_control_test.cpp /Fo:build\raw_axis_control_test.obj /Fe:build\raw_axis_control_test.exe
if %errorlevel% neq 0 exit /b 1
build\raw_axis_control_test.exe
if %errorlevel% neq 0 exit /b 1
cl /nologo /O2 /MT /W4 /std:c++20 /EHsc /Isrc tests\throttle_keys_test.cpp /Fo:build\throttle_keys_test.obj /Fe:build\throttle_keys_test.exe
if %errorlevel% neq 0 exit /b 1
build\throttle_keys_test.exe
if %errorlevel% neq 0 exit /b 1
cl /nologo /O2 /MT /W4 /std:c++20 /EHsc /Isrc tests\status_channel_test.cpp /Fo:build\status_channel_test.obj /Fe:build\status_channel_test.exe
if %errorlevel% neq 0 exit /b 1
build\status_channel_test.exe
if %errorlevel% neq 0 exit /b 1
cl /nologo /O2 /MT /W4 /std:c++20 /EHsc /Isrc tests\processor_contract_test.cpp /Fo:build\processor_contract_test.obj /Fe:build\processor_contract_test.exe
if %errorlevel% neq 0 exit /b 1
build\processor_contract_test.exe
if %errorlevel% neq 0 exit /b 1
cl /nologo /O2 /MT /W4 /std:c++20 /EHsc /Isrc tests\absolute_hook_test.cpp /Fo:build\absolute_hook_test.obj /Fe:build\absolute_hook_test.exe
if %errorlevel% neq 0 exit /b 1
build\absolute_hook_test.exe
if %errorlevel% neq 0 exit /b 1
ml64 /nologo /c /Fobuild\processor_bridge.obj src\processor_bridge.asm
if %errorlevel% neq 0 exit /b 1
cl /nologo /O2 /MT /W4 /std:c++20 /EHsc /LD /Isrc src\throttle_control.cpp build\processor_bridge.obj /Fo:build\throttle_control.obj /Fe:build\ac8_throttle_control.dll /link /Brepro user32.lib
if %errorlevel% neq 0 exit /b 1
dumpbin /nologo /exports build\ac8_throttle_control.dll > build\exports.txt
dumpbin /nologo /dependents build\ac8_throttle_control.dll > build\dependents.txt
type build\exports.txt
type build\dependents.txt
exit /b 0
