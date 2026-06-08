@echo off
setlocal
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
  echo Codex95 requires Node.js.
  echo Install it from https://nodejs.org
  pause
  exit /b 1
)
set CODEX95_MOCK=1
node server.mjs
pause
