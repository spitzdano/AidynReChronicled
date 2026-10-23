@echo off
rem Add Aidyn ReChronicled to Steam as a non-Steam game, with its library art (add-to-steam.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0add-to-steam.ps1"
echo.
pause
