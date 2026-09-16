@echo off
:: Pedir permisos de Administrador automáticamente (requerido para leer la memoria de Azahar)
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
title Project ORAS MMO - Cliente PC

echo ========================================================
echo   Project ORAS MMO - Inicializando Cliente PC
echo ========================================================
echo.
echo Comprobando requisitos...
python -m pip install -q pymem websockets psutil

if %errorlevel% neq 0 (
    echo [ERROR] No se pudo instalar pymem o websockets. Asegurate de tener Python instalado y agregado al PATH.
    pause
    exit
)

echo Todo listo. Abriendo el cliente...
echo.
python pc_client.py

pause
