@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion
title Pokemon ORAS MMO - Servidor / Server
color 0A

echo ============================================================
echo    Pokemon ORAS MMO  ^|  Servidor / Relay server
echo ============================================================
echo.

python --version >nul 2>&1
IF ERRORLEVEL 1 (
    color 0C
    echo [ERROR] Python no encontrado / Python not found.
    echo         https://www.python.org/downloads/  ^("Add Python to PATH"^)
    echo.
    pause & exit /b 1
)
FOR /F "tokens=*" %%v IN ('python --version 2^>^&1') DO SET PYVER=%%v
echo [OK] %PYVER%
echo.

if "%PORT%"=="" set "PORT=9000"
echo [INFO] IP local de este PC / This PC's local IP (same Wi-Fi):
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4" ^| findstr /v "127.0.0.1"') do (
    set "IP=%%a"
    set "IP=!IP: =!"
    echo         !IP!
)
echo.
echo [INFO] Jugadores en este mismo PC / Players on this same PC: 127.0.0.1 (por defecto / default)
echo [INFO] Amigos lejos / Friends far away: ZeroTier o/or Tailscale ^(da tu IP de esa red / give your IP on that
echo        network^), abre el TCP %PORT% en tu router / open TCP %PORT% on your router, o/or un VPS.
echo        Guia / Guide: README ^> "Jugar por Internet sin servidor contratado".
echo [INFO] Cada jugador: mmo_server.txt = "IP" o "IP:puerto" / Each player: mmo_server.txt = "IP" or "IP:port"
echo.
echo [INFO] Puerto / Port: %PORT%   ^(Ctrl+C para apagar / to stop^)
echo.
python "%~dp0server.py"

IF ERRORLEVEL 1 (
    color 0C
    echo.
    echo [ERROR] El servidor se cerro / The server stopped unexpectedly.
)
pause
endlocal
