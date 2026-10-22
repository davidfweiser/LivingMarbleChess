@echo off
rem Windows installer: builds the game, then packs it into
rem   build\LivingMarble-<version>-setup.exe
rem Needs Visual Studio 2022 or later (with C++ and CMake) and Inno Setup 6
rem (https://jrsoftware.org/isdl.php, or: winget install JRSoftware.InnoSetup).
setlocal
cd /d "%~dp0.."

rem CMake: the one on PATH, or else the one that comes with Visual Studio
set "CMAKE=cmake"
where cmake >nul 2>nul && goto :build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_cmake
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do set "CMAKE=%%i"
if "%CMAKE%"=="cmake" goto :no_cmake

:build
"%CMAKE%" -S . -B build
if errorlevel 1 exit /b 1
"%CMAKE%" --build build --config Release
if errorlevel 1 exit /b 1

rem Inno Setup's compiler: on PATH, or where its installer puts it
set "ISCC="
for %%p in (ISCC.exe) do set "ISCC=%%~$PATH:p"
if not defined ISCC if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC goto :no_iscc

"%ISCC%" /Qp windows\installer.iss
if errorlevel 1 exit /b 1
echo.
for /f "delims=" %%f in ('dir /b /o:d build\LivingMarble-*-setup.exe') do set "SETUP=%%f"
echo Built: build\%SETUP%
exit /b 0

:no_cmake
echo CMake not found: install Visual Studio with "Desktop development with C++",
echo or run this from a Developer Command Prompt.
exit /b 1

:no_iscc
echo Inno Setup 6 not found: install it from https://jrsoftware.org/isdl.php
echo or with: winget install JRSoftware.InnoSetup
exit /b 1
