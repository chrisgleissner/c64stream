@echo off
REM C64 Stream - Windows Build Wrapper
REM This script calls local-build.sh using bash from Git Bash or Cygwin

setlocal enabledelayedexpansion

REM Check for bash in common locations
set BASH_EXE=

REM Try Git Bash locations
if exist "C:\Program Files\Git\bin\bash.exe" (
    set BASH_EXE=C:\Program Files\Git\bin\bash.exe
    goto :found_bash
)
if exist "C:\Program Files (x86)\Git\bin\bash.exe" (
    set BASH_EXE=C:\Program Files (x86)\Git\bin\bash.exe
    goto :found_bash
)

REM Try Cygwin locations
if exist "C:\cygwin64\bin\bash.exe" (
    set BASH_EXE=C:\cygwin64\bin\bash.exe
    goto :found_bash
)
if exist "C:\cygwin\bin\bash.exe" (
    set BASH_EXE=C:\cygwin\bin\bash.exe
    goto :found_bash
)

REM Try finding bash in PATH
where bash >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set BASH_EXE=bash
    goto :found_bash
)

REM Bash not found
echo ERROR: bash not found. Please install Git for Windows or Cygwin.
echo.
echo Download Git for Windows from: https://git-scm.com/download/win
echo.
exit /b 1

:found_bash
echo Using bash: %BASH_EXE%
echo.

REM Get the script directory
set SCRIPT_DIR=%~dp0
REM Remove trailing backslash
if "%SCRIPT_DIR:~-1%"=="\" set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

REM Call the bash script with all arguments, let bash handle the path
"%BASH_EXE%" "%SCRIPT_DIR%\local-build.sh" %*

exit /b %ERRORLEVEL%
