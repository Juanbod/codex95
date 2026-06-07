@echo off
rem Run from a Visual C++ 6.0 command prompt.
cl /O1 /G5 /GF /W3 /MT codex95.c wsock32.lib gdi32.lib /Fe:CODEX95.EXE /link /OPT:REF
cl /O1 /G5 /GF /W3 /MT codex95_gui.c wsock32.lib shell32.lib gdi32.lib /Fe:CODEX95W.EXE /link /subsystem:windows /OPT:REF
