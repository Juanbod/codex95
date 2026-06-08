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
start "" cmd /c "ping 127.0.0.1 -n 3 >nul & start http://127.0.0.1:8787/setup"
node server.mjs
pause
