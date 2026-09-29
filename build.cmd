@echo off
rem MBROLA NG - developer build (Windows, MSVC + Ninja from Visual Studio)
rem Copyright (c) 2026 Darko Milosevic
rem SPDX-License-Identifier: GPL-2.0-or-later
rem
rem   build.cmd            x64 Release build into build\x64
rem   build.cmd x86        x86 build into build\x86
rem   build.cmd x64 Debug  debug build
setlocal
set ARCH=%1
if "%ARCH%"=="" set ARCH=x64
set CONFIG=%2
if "%CONFIG%"=="" set CONFIG=Release

set VSROOT=
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSROOT=%%i
if "%VSROOT%"=="" (
  echo Visual Studio not found.
  exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul || exit /b 1

set BUILDDIR=%~dp0build\%ARCH%
cmake -S "%~dp0." -B "%BUILDDIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% || exit /b 1
cmake --build "%BUILDDIR%" || exit /b 1
rem every folder in languages\ is a language: compile and test it
for /d %%L in ("%~dp0languages\*") do (
  "%BUILDDIR%\langc.exe" "%%L" -o "%BUILDDIR%\%%~nxL.dat" || exit /b 1
)
endlocal
