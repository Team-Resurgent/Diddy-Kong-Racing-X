@echo off
rem Diddy-Kong-Racing-X: one double-click from your ROM to Diddy-Kong-Racing-X.iso.
rem
rem   1. put your Diddy Kong Racing ROM (USA, v1.0 or v1.1) in this folder,
rem      named baserom.z64
rem   2. run this file (double-click, or "build.bat" in a console)
rem
rem It finds a Python 3.8 or later and hands over to setup.py, which checks
rem the ROM, builds the decomp's asset tool, extracts the game from the ROM
rem and builds. Arguments are passed on: build.bat --check,
rem build.bat --config Debug, build.bat --skip-build, build.bat --unlock-all.
setlocal EnableExtensions
set "DKRX_SELF=%~f0"
rem A Visual Studio build of an RXDK project runs its Pre-Build Event with no
rem PATH at all; put the Windows folders back so py and where resolve.
if not defined PATH set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem"
cd /d "%~dp0"
title Diddy-Kong-Racing-X build

echo.
echo  Diddy-Kong-Racing-X -- Diddy Kong Racing for the original Xbox
echo.

set "DKRX_ROM="
for %%R in (baserom.z64 dkr.z64 baserom.v64 dkr.v64 baserom.n64 dkr.n64) do (
  if not defined DKRX_ROM if exist "%%R" set "DKRX_ROM=%%R"
  if not defined DKRX_ROM if exist "baseroms\%%R" set "DKRX_ROM=baseroms\%%R"
)
if not defined DKRX_ROM (
  echo  Your ROM is not here. Copy your Diddy Kong Racing ROM ^(USA, v1.0 or v1.1^)
  echo  into
  echo      %~dp0
  echo  and name it  baserom.z64  then run this again.
  goto :fail
)

set "DKRX_PY="
for %%V in (3.13 3.12 3.14 3.15 3.11 3.10 3.9 3.8) do (
  if not defined DKRX_PY (
    py -%%V -c "import sys" >nul 2>nul && set "DKRX_PY=py -%%V"
  )
)
if not defined DKRX_PY (
  python -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>nul && set "DKRX_PY=python"
)
if not defined DKRX_PY (
  echo  No Python 3.8 or later was found. Install it from
  echo      https://www.python.org/downloads/
  echo  ^(tick "Add python.exe to PATH"^), then run this again.
  goto :fail
)

echo  Python: %DKRX_PY%
echo.
%DKRX_PY% setup.py %*
set "DKRX_RC=%ERRORLEVEL%"
if not "%DKRX_RC%"=="0" goto :failrc
echo.
echo  Done.
call :pause
exit /b 0

:failrc
echo.
echo  The build stopped (exit code %DKRX_RC%). The messages above say why.
call :pause
exit /b %DKRX_RC%

:fail
echo.
call :pause
exit /b 1

rem ---- keep a double-clicked window open --------------------------------
:pause
if defined DKRX_NONINTERACTIVE exit /b 0
echo %cmdcmdline% | find /i "%DKRX_SELF%" >nul
if not errorlevel 1 pause
exit /b 0
