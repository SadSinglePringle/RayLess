@echo off
title ASTG Real-Time GI Laboratory
cd /d "%~dp0"
echo ===============================================================
echo   Launching ASTG Real-Time GI Laboratory in Godot 4.7...
echo ===============================================================
"C:\Users\Brand\Documents\RacingSimGame\.tools\godot\Godot_v4.7.1-stable_win64.exe" --path "%~dp0"
if %errorlevel% neq 0 (
    echo.
    echo If the window closed or had an issue, launching with console for debug:
    "C:\Users\Brand\Documents\RacingSimGame\.tools\godot\Godot_v4.7.1-stable_win64_console.exe" --path "%~dp0"
    pause
)
