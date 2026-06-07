@echo off
if "%OPENAI_API_KEY%"=="" (
  echo OPENAI_API_KEY is not set.
  echo Set it in Windows before starting the bridge.
  pause
  exit /b 1
)
node server.mjs
pause
