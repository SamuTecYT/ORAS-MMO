@echo off
NET SESSION >nul 2>&1
if %errorLevel% == 0 (
    goto :Admin
) else (
    echo Solicitando permisos de administrador...
    powershell -Command "Start-Process '%0' -Verb RunAs"
    exit
)

:Admin
cd /d "%~dp0"
title Project ORAS MMO - Inyeccion 3D (Escaneando Entidades...)

echo ========================================================
echo   Hackeo 3D - Buscando Punteros de Entidades (NPCs)
echo ========================================================
echo.
echo POR FAVOR, ASEGURATE DE:
echo 1. Tener Azahar abierto y estar cargado en una ruta del juego.
echo 2. CERRAR la ventana del Cliente MMO (Jugar_MMO_PC) si la tienes abierta.
echo.
pause

python entity_scanner.py

echo.
echo Escaneo terminado. Toma una captura de estos resultados.
pause
