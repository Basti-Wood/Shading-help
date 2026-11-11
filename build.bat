@echo off
setlocal
cd /d "%~dp0"

rem Shading Help: dependency-free C++17 build using Microsoft's free C++ tools.
rem --ci suppresses the final pause. No downloads or administrator rights needed.
where cl.exe >nul 2>nul
if not errorlevel 1 goto compiler_ready

set "SHADING_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%SHADING_VSWHERE%" goto no_compiler
set "SHADING_VS="
for /f "usebackq tokens=*" %%i in (`"%SHADING_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "SHADING_VS=%%i"
if not defined SHADING_VS goto no_compiler
call "%SHADING_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 goto failed

:compiler_ready
if not exist build mkdir build
if not exist dist mkdir dist
if not exist build\obj mkdir build\obj

echo Building Shading Help for Windows...
cl /nologo /std:c++17 /O2 /EHsc /MT /W4 /utf-8 /D_WIN32_WINNT=0x0A00 /Fo:build\obj\ src\core.cpp src\progressive.cpp src\windows.cpp /Fe:dist\ShadingHelp.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib
if errorlevel 1 goto failed

echo Building numerical and renderer tests...
cl /nologo /std:c++17 /O2 /EHsc /MT /W4 /utf-8 /Fo:build\obj\ src\core.cpp src\progressive.cpp tests\tests.cpp /Fe:build\tests.exe
if errorlevel 1 goto failed
build\tests.exe
if errorlevel 1 goto failed

echo Building command-line renderer...
cl /nologo /std:c++17 /O2 /EHsc /MT /W4 /utf-8 /Fo:build\obj\ src\core.cpp src\cli.cpp /Fe:build\shading-render.exe
if errorlevel 1 goto failed
copy /y README.md dist\README.md >nul
if exist LICENSE copy /y LICENSE dist\LICENSE >nul
echo.
echo Success: dist\ShadingHelp.exe
echo The app is portable and works offline. No runtime installation is required.
if /i not "%~1"=="--ci" pause
exit /b 0

:no_compiler
echo.
echo Microsoft C++ Build Tools were not found.
echo Install the free Build Tools for Visual Studio with:
echo   Desktop development with C++
echo   MSVC x64/x86 tools and Windows 10 or 11 SDK
echo https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
echo Then run this batch file again. The Visual Studio IDE is not required.
if /i not "%~1"=="--ci" pause
exit /b 1

:failed
echo.
echo Build or tests failed. Read the error above; no success is being reported.
if /i not "%~1"=="--ci" pause
exit /b 1
