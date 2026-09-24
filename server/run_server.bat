@echo off
setlocal EnableDelayedExpansion
title Project ORAS MMO — Servidor v3
color 0A

echo ============================================================
echo    Project ORAS MMO  ^|  Servidor de Relé v3
echo ============================================================
echo.

:: ── Verificar Python ──────────────────────────────────────────
python --version >nul 2>&1
IF ERRORLEVEL 1 (
    color 0C
    echo [ERROR] Python no encontrado.
    echo Descargalo de: https://www.python.org/downloads/
    echo Marca "Add Python to PATH" al instalar.
    echo.
    pause & exit /b 1
)
FOR /F "tokens=*" %%v IN ('python --version 2^>^&1') DO SET PYVER=%%v
echo [OK] %PYVER%
echo.

:: ── Mostrar IPs disponibles ──────────────────────────────────
echo [INFO] IP Local (para amigos en la MISMA red Wi-Fi):
echo.
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4" ^| findstr /v "127.0.0.1"') do (
    set "IP=%%a"
    set "IP=!IP: =!"
    echo         !IP!
)
echo.
echo [INFO] Para jugar por INTERNET con amigos a distancia (ZeroTier):
echo        - Instala y abre ZeroTier One (incluido en el paquete).
echo        - Conectate a tu red de ZeroTier.
echo        - Tu "Managed IP" de ZeroTier (ej. 10.147.x.x o 192.168.19x.x) es la que
echo          debes dar a tus amigos para que la ingresen en el juego (SELECT -^> Cambiar IP).
echo.

:: ── Iniciar servidor ─────────────────────────────────────────
echo [INFO] Puerto: 9000
echo [INFO] Esperando jugadores... (Ctrl+C para apagar)
echo.
python server.py

IF ERRORLEVEL 1 (
    color 0C
    echo.
    echo [ERROR] El servidor se cerró inesperadamente.
)
pause
endlocal
