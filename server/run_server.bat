@echo off
setlocal EnableDelayedExpansion
title Project ORAS MMO - Relay Server
color 0A

echo ============================================================
echo   Project ORAS MMO ^| WebSocket Relay Server Launcher
echo ============================================================
echo.

REM --- Check Python is installed ---
python --version >nul 2>&1
IF ERRORLEVEL 1 (
    color 0C
    echo [ERROR] Python is not installed or not in PATH.
    echo         Download Python 3.8+ from https://www.python.org/downloads/
    echo         Make sure to check "Add Python to PATH" during install.
    pause
    exit /b 1
)

FOR /F "tokens=*" %%v IN ('python --version 2^>^&1') DO SET PYVER=%%v
echo [OK] Found %PYVER%
echo.

REM --- Install/update dependencies ---
echo [INFO] Installing dependencies from requirements.txt...
python -m pip install -r requirements.txt --quiet
IF ERRORLEVEL 1 (
    color 0C
    echo [ERROR] pip install failed. Check your internet connection.
    pause
    exit /b 1
)
echo [OK] Dependencies ready.
echo.

REM --- Display LAN IP address so players know where to connect ---
echo [INFO] Your LAN IP addresses (share one with your players):
echo -------------------------------------------------------
ipconfig | findstr /i "IPv4" | findstr /v "127.0.0.1"
echo -------------------------------------------------------
echo.
echo [INFO] Tell players to connect to:  ws://YOUR_LAN_IP:8765
echo [INFO] Internet players: use public IP + port-forward 8765/TCP
echo        or deploy free on Railway/Render (see deploy_guide.md)
echo.

REM --- Optional: Allow PORT override via environment variable ---
IF "%PORT%"=="" SET PORT=8765
echo [INFO] Starting server on port %PORT%...
echo        Press Ctrl+C to stop the server.
echo.

REM --- Launch the server ---
python server.py

REM --- Handle abnormal exit ---
IF ERRORLEVEL 1 (
    color 0C
    echo.
    echo [ERROR] Server exited with an error. Check output above.
)
pause
endlocal
