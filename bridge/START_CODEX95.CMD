@echo off
start "" cmd /c "ping 127.0.0.1 -n 3 >nul & start http://127.0.0.1:8787/setup"
node server.mjs
pause
