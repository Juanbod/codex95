@echo off
rem Run from a Visual C++ 6.0 command prompt.
cd /d "%~dp0"
rc CODEX95.RC
cl /O1 /G5 /GF /W3 /MT codex95.c CODEX95.RES wsock32.lib gdi32.lib /Fe:CODEX95.EXE /link /OPT:REF
cl /O1 /G5 /GF /W3 /MT codex95_gui.c CODEX95.RES wsock32.lib shell32.lib gdi32.lib /Fe:CODEX95W.EXE /link /subsystem:windows /OPT:REF
