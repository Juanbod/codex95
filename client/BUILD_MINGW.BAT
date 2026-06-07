@echo off
setlocal
cd /d "%~dp0"

if "%CC%"=="" set CC=gcc
if not exist ..\build mkdir ..\build

set COMMON=-std=gnu89 -Os -march=pentium -DWINVER=0x0400 -D_WIN32_WINNT=0x0400 -Wall -Wextra -Wpedantic -static -s
set PE95=-Wl,--major-subsystem-version,4,--minor-subsystem-version,0,--disable-dynamicbase,--disable-nxcompat,--stack,1048576

%CC% %COMMON% codex95.c -o ..\build\CODEX95.EXE -lwsock32 -lgdi32 %PE95%
if errorlevel 1 exit /b 1

%CC% %COMMON% -mwindows codex95_gui.c -o ..\build\CODEX95W.EXE -lwsock32 -lshell32 -lgdi32 %PE95%
if errorlevel 1 exit /b 1

echo Built ..\build\CODEX95.EXE and ..\build\CODEX95W.EXE
